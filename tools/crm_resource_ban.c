/*
 * Copyright 2004-2018 Andrew Beekhof <andrew@beekhof.net>
 *
 * This source code is licensed under the GNU General Public License version 2
 * or later (GPLv2+) WITHOUT ANY WARRANTY.
 */

#include <crm_resource.h>

#define XPATH_MAX 1024

char *move_lifetime = NULL;

static char *
parse_cli_lifetime(const char *input)
{
    char *later_s = NULL;
    crm_time_t *now = NULL;
    crm_time_t *later = NULL;
    crm_time_t *duration = NULL;

    if (input == NULL) {
        return NULL;
    }

    duration = crm_time_parse_duration(move_lifetime);
    if (duration == NULL) {
        CMD_ERR("Invalid duration specified: %s", move_lifetime);
        CMD_ERR("Please refer to"
                " http://en.wikipedia.org/wiki/ISO_8601#Durations"
                " for examples of valid durations");
        return NULL;
    }

    now = crm_time_new(NULL);
    later = crm_time_add(now, duration);
    crm_time_log(LOG_INFO, "now     ", now,
                 crm_time_log_date | crm_time_log_timeofday | crm_time_log_with_timezone);
    crm_time_log(LOG_INFO, "later   ", later,
                 crm_time_log_date | crm_time_log_timeofday | crm_time_log_with_timezone);
    crm_time_log(LOG_INFO, "duration", duration, crm_time_log_date | crm_time_log_timeofday);
    later_s = crm_time_as_string(later, crm_time_log_date | crm_time_log_timeofday | crm_time_log_with_timezone);
    printf("Migration will take effect until: %s\n", later_s);

    crm_time_free(duration);
    crm_time_free(later);
    crm_time_free(now);
    return later_s;
}

int
cli_resource_ban(const char *rsc_id, const char *host, GListPtr allnodes, cib_t * cib_conn)
{
    char *later_s = NULL;
    int rc = pcmk_ok;
    xmlNode *fragment = NULL;
    xmlNode *location = NULL;

    if(host == NULL) {
        GListPtr n = allnodes;
        for(; n && rc == pcmk_ok; n = n->next) {
            node_t *target = n->data;

            rc = cli_resource_ban(rsc_id, target->details->uname, NULL, cib_conn);
        }
        return rc;
    }

    later_s = parse_cli_lifetime(move_lifetime);
    if(move_lifetime && later_s == NULL) {
        return -EINVAL;
    }

    fragment = create_xml_node(NULL, XML_CIB_TAG_CONSTRAINTS);

    location = create_xml_node(fragment, XML_CONS_TAG_RSC_LOCATION);
    crm_xml_set_id(location, "cli-ban-%s-on-%s", rsc_id, host);

    if (BE_QUIET == FALSE) {
        CMD_ERR("WARNING: Creating rsc_location constraint '%s'"
                " with a score of -INFINITY for resource %s"
                " on %s.", ID(location), rsc_id, host);
        CMD_ERR("\tThis will prevent %s from %s on %s until the constraint "
                "is removed using the clear option or by editing the CIB "
                "with an appropriate tool",
                rsc_id, (scope_master? "being promoted" : "running"), host);
        CMD_ERR("\tThis will be the case even if %s is"
                " the last node in the cluster", host);
    }

    crm_xml_add(location, XML_LOC_ATTR_SOURCE, rsc_id);
    if(scope_master) {
        crm_xml_add(location, XML_RULE_ATTR_ROLE, RSC_ROLE_MASTER_S);
    } else {
        crm_xml_add(location, XML_RULE_ATTR_ROLE, RSC_ROLE_STARTED_S);
    }

    if (later_s == NULL) {
        /* Short form */
        crm_xml_add(location, XML_CIB_TAG_NODE, host);
        crm_xml_add(location, XML_RULE_ATTR_SCORE, CRM_MINUS_INFINITY_S);

    } else {
        xmlNode *rule = create_xml_node(location, XML_TAG_RULE);
        xmlNode *expr = create_xml_node(rule, XML_TAG_EXPRESSION);

        crm_xml_set_id(rule, "cli-ban-%s-on-%s-rule", rsc_id, host);
        crm_xml_add(rule, XML_RULE_ATTR_SCORE, CRM_MINUS_INFINITY_S);
        crm_xml_add(rule, XML_RULE_ATTR_BOOLEAN_OP, "and");

        crm_xml_set_id(expr, "cli-ban-%s-on-%s-expr", rsc_id, host);
        crm_xml_add(expr, XML_EXPR_ATTR_ATTRIBUTE, CRM_ATTR_UNAME);
        crm_xml_add(expr, XML_EXPR_ATTR_OPERATION, "eq");
        crm_xml_add(expr, XML_EXPR_ATTR_VALUE, host);
        crm_xml_add(expr, XML_EXPR_ATTR_TYPE, "string");

        expr = create_xml_node(rule, "date_expression");
        crm_xml_set_id(expr, "cli-ban-%s-on-%s-lifetime", rsc_id, host);
        crm_xml_add(expr, "operation", "lt");
        crm_xml_add(expr, "end", later_s);
    }

    crm_log_xml_notice(fragment, "Modify");
    rc = cib_conn->cmds->update(cib_conn, XML_CIB_TAG_CONSTRAINTS, fragment, cib_options);

    free_xml(fragment);
    free(later_s);
    return rc;
}


int
cli_resource_prefer(const char *rsc_id, const char *host, cib_t * cib_conn)
{
    char *later_s = parse_cli_lifetime(move_lifetime);
    int rc = pcmk_ok;
    xmlNode *location = NULL;
    xmlNode *fragment = NULL;

    if(move_lifetime && later_s == NULL) {
        return -EINVAL;
    }

    if(cib_conn == NULL) {
        free(later_s);
        return -ENOTCONN;
    }

    fragment = create_xml_node(NULL, XML_CIB_TAG_CONSTRAINTS);

    location = create_xml_node(fragment, XML_CONS_TAG_RSC_LOCATION);
    crm_xml_set_id(location, "cli-prefer-%s", rsc_id);

    crm_xml_add(location, XML_LOC_ATTR_SOURCE, rsc_id);
    if(scope_master) {
        crm_xml_add(location, XML_RULE_ATTR_ROLE, RSC_ROLE_MASTER_S);
    } else {
        crm_xml_add(location, XML_RULE_ATTR_ROLE, RSC_ROLE_STARTED_S);
    }

    if (later_s == NULL) {
        /* Short form */
        crm_xml_add(location, XML_CIB_TAG_NODE, host);
        crm_xml_add(location, XML_RULE_ATTR_SCORE, CRM_INFINITY_S);

    } else {
        xmlNode *rule = create_xml_node(location, XML_TAG_RULE);
        xmlNode *expr = create_xml_node(rule, XML_TAG_EXPRESSION);

        crm_xml_set_id(rule, "cli-prefer-rule-%s", rsc_id);
        crm_xml_add(rule, XML_RULE_ATTR_SCORE, CRM_INFINITY_S);
        crm_xml_add(rule, XML_RULE_ATTR_BOOLEAN_OP, "and");

        crm_xml_set_id(expr, "cli-prefer-expr-%s", rsc_id);
        crm_xml_add(expr, XML_EXPR_ATTR_ATTRIBUTE, CRM_ATTR_UNAME);
        crm_xml_add(expr, XML_EXPR_ATTR_OPERATION, "eq");
        crm_xml_add(expr, XML_EXPR_ATTR_VALUE, host);
        crm_xml_add(expr, XML_EXPR_ATTR_TYPE, "string");

        expr = create_xml_node(rule, "date_expression");
        crm_xml_set_id(expr, "cli-prefer-lifetime-end-%s", rsc_id);
        crm_xml_add(expr, "operation", "lt");
        crm_xml_add(expr, "end", later_s);
    }

    crm_log_xml_info(fragment, "Modify");
    rc = cib_conn->cmds->update(cib_conn, XML_CIB_TAG_CONSTRAINTS, fragment, cib_options);

    free_xml(fragment);
    free(later_s);
    return rc;
}

int
cli_resource_clear(const char *rsc_id, const char *host, GListPtr allnodes, cib_t * cib_conn)
{
    int rc = pcmk_ok;
    xmlNode *fragment = NULL;
    xmlNode *location = NULL;

    if(cib_conn == NULL) {
        return -ENOTCONN;
    }

    fragment = create_xml_node(NULL, XML_CIB_TAG_CONSTRAINTS);

    if(host) {
        location = create_xml_node(fragment, XML_CONS_TAG_RSC_LOCATION);
        crm_xml_set_id(location, "cli-ban-%s-on-%s", rsc_id, host);

    } else {
        GListPtr n = allnodes;
        for(; n; n = n->next) {
            node_t *target = n->data;

            location = create_xml_node(fragment, XML_CONS_TAG_RSC_LOCATION);
            crm_xml_set_id(location, "cli-ban-%s-on-%s",
                           rsc_id, target->details->uname);
        }
    }

    location = create_xml_node(fragment, XML_CONS_TAG_RSC_LOCATION);
    crm_xml_set_id(location, "cli-prefer-%s", rsc_id);
    if(host && do_force == FALSE) {
        crm_xml_add(location, XML_CIB_TAG_NODE, host);
    }

    crm_log_xml_info(fragment, "Delete");
    rc = cib_conn->cmds->remove(cib_conn, XML_CIB_TAG_CONSTRAINTS, fragment, cib_options);
    if (rc == -ENXIO) {
        rc = pcmk_ok;

    } else if (rc != pcmk_ok) {
        goto bail;
    }

  bail:
    free_xml(fragment);
    return rc;
}

static char *
build_clear_xpath_string(xmlNode *constraint_node, const char *rsc, const char *node, bool scope_master)
{
    int offset = 0;
    char *xpath_string = NULL;
    char *first_half = NULL;
    char *rsc_role_substr = NULL;
    char *date_substr = NULL;

    if (crm_starts_with(ID(constraint_node), "cli-ban-")) {
        date_substr = crm_strdup_printf("//date_expression[@id='%s-lifetime']",
                                        ID(constraint_node));

    } else if (crm_starts_with(ID(constraint_node), "cli-prefer-")) {
        date_substr = crm_strdup_printf("//date_expression[@id='cli-prefer-lifetime-end-%s']",
                                        crm_element_value(constraint_node, "rsc"));
    } else {
        return NULL;
    }

    first_half = calloc(1, XPATH_MAX);
    offset += snprintf(first_half + offset, XPATH_MAX - offset, "//rsc_location");

    if (node != NULL || rsc != NULL || scope_master == TRUE) {
        offset += snprintf(first_half + offset, XPATH_MAX - offset, "[");

        if (node != NULL) {
            if (rsc != NULL || scope_master == TRUE) {
                offset += snprintf(first_half + offset, XPATH_MAX - offset, "@node='%s' and ", node);
            } else {
                offset += snprintf(first_half + offset, XPATH_MAX - offset, "@node='%s'", node);
            }
        }

        if (rsc != NULL && scope_master == TRUE) {
            rsc_role_substr = crm_strdup_printf("@rsc='%s' and @role='%s'", rsc, RSC_ROLE_MASTER_S);
            offset += snprintf(first_half + offset, XPATH_MAX - offset, "@rsc='%s' and @role='%s']", rsc, RSC_ROLE_MASTER_S);
        } else if (rsc != NULL) {
            rsc_role_substr = crm_strdup_printf("@rsc='%s'", rsc);
            offset += snprintf(first_half + offset, XPATH_MAX - offset, "@rsc='%s']", rsc);
        } else if (scope_master == TRUE) {
            rsc_role_substr = crm_strdup_printf("@role='%s'", RSC_ROLE_MASTER_S);
            offset += snprintf(first_half + offset, XPATH_MAX - offset, "@role='%s']", RSC_ROLE_MASTER_S);
        } else {
            offset += snprintf(first_half + offset, XPATH_MAX - offset, "]");
        }
    }

    if (node != NULL) {
        if (rsc_role_substr != NULL) {
            xpath_string = crm_strdup_printf("%s|//rsc_location[%s]/rule[expression[@attribute='#uname' and @value='%s']]%s",
                                             first_half, rsc_role_substr, node, date_substr);
        } else {
            xpath_string = crm_strdup_printf("%s|//rsc_location/rule[expression[@attribute='#uname' and @value='%s']]%s",
                                             first_half, node, date_substr);
        }
    } else {
        xpath_string = crm_strdup_printf("%s%s", first_half, date_substr);
    }

    free(first_half);
    free(date_substr);
    free(rsc_role_substr);

    return xpath_string;
}

int
cli_resource_clear_all_expired(xmlNode *root, cib_t *cib_conn, const char *rsc, const char *node, bool scope_master)
{
    xmlXPathObject *xpathObj = NULL;
    xmlNode *cib_constraints = NULL;
    crm_time_t *now = crm_time_new(NULL);
    int i;
    int rc = pcmk_ok;

    cib_constraints = get_object_root(XML_CIB_TAG_CONSTRAINTS, root);
    xpathObj = xpath_search(cib_constraints, "//" XML_CONS_TAG_RSC_LOCATION);

    for (i = 0; i < numXpathResults(xpathObj); i++) {
        xmlNode *constraint_node = getXpathResult(xpathObj, i);
        xmlNode *date_expr_node = NULL;
        crm_time_t *end = NULL;
        char *xpath_string = NULL;

        xpath_string = build_clear_xpath_string(constraint_node, rsc, node, scope_master);
        if (xpath_string == NULL) {
            continue;
        }

        date_expr_node = get_xpath_object(xpath_string, constraint_node, LOG_DEBUG);
        if (date_expr_node == NULL) {
            free(xpath_string);
            continue;
        }

        /* And then finally, see if the date expression is expired.  If so,
         * clear the constraint.
         */
        end = crm_time_new(crm_element_value(date_expr_node, "end"));

        if (crm_time_compare(now, end) == 1) {
            xmlNode *fragment = NULL;
            xmlNode *location = NULL;

            fragment = create_xml_node(NULL, XML_CIB_TAG_CONSTRAINTS);
            location = create_xml_node(fragment, XML_CONS_TAG_RSC_LOCATION);
            crm_xml_set_id(location, "%s", ID(constraint_node));
            crm_log_xml_info(fragment, "Delete");

            rc = cib_conn->cmds->remove(cib_conn, XML_CIB_TAG_CONSTRAINTS,
                                        fragment, cib_options);
            if (rc != pcmk_ok) {
                free(xpath_string);
                goto bail;
            }

            free_xml(fragment);
        }

        crm_time_free(end);
        free(xpath_string);
    }

    rc = pcmk_ok;

bail:
    freeXpathObject(xpathObj);
    crm_time_free(now);
    return rc;
}
