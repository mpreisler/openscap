/*
 * Copyright 2017 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * * Authors:
 * 		Katarina Jankov <kj226@cornell.edu>
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <math.h>

#include <libxml/xmlreader.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/parser.h>
#include <libxml/xpathInternals.h>

#include "public/cvrf.h"
#include "cvrf_priv.h"

#include "common/list.h"
#include "common/_error.h"
#include "common/xmltext_priv.h"
#include "common/elements.h"
#include "common/oscap_string.h"
#include "common/util.h"

#include "CPE/cpelang_priv.h"
#include "CVSS/cvss_priv.h"
#include "CVSS/public/cvss_score.h"

#include "source/oscap_source_priv.h"
#include "source/public/oscap_source.h"
#include "oscap_helpers.h"


/***************************************************************************
 ***************************************************************************
 * Vulnerability offshoot of main CVRF model
 *
 */

/***************************************************************************
 * CVRF Remediation
 */
struct cvrf_remediation {
	cvrf_remediation_type_t type;
	char *date;
	char *description;
	char *url;
	char *entitlement;
	struct oscap_stringlist *product_ids;
	struct oscap_stringlist *group_ids;
};
OSCAP_ACCESSOR_STRING(cvrf_remediation, date)
OSCAP_ACCESSOR_STRING(cvrf_remediation, description)
OSCAP_ACCESSOR_STRING(cvrf_remediation, url)
OSCAP_ACCESSOR_STRING(cvrf_remediation, entitlement)

cvrf_remediation_type_t cvrf_remediation_get_type(struct cvrf_remediation *remed) {
	return remed->type;
}

struct oscap_string_iterator *cvrf_remediation_get_product_ids(struct cvrf_remediation *remed) {
	return oscap_stringlist_get_strings(remed->product_ids);
}
struct oscap_string_iterator *cvrf_remediation_get_group_ids(struct cvrf_remediation *remed) {
	return oscap_stringlist_get_strings(remed->group_ids);
}

struct cvrf_remediation *cvrf_remediation_new() {
	struct cvrf_remediation *ret =  malloc(sizeof(struct cvrf_remediation));
	if (ret == NULL)
		return NULL;

	ret->type = CVRF_REMEDIATION_UNKNOWN;
	ret->date = NULL;
	ret->description = NULL;
	ret->url = NULL;
	ret->entitlement = NULL;
	ret->product_ids = oscap_stringlist_new();
	ret->group_ids = oscap_stringlist_new();
	return ret;
}

void cvrf_remediation_free(struct cvrf_remediation *remed) {
	if (remed == NULL)
		return;

	free(remed->date);
	free(remed->description);
	free(remed->url);
	free(remed->entitlement);
	oscap_stringlist_free(remed->product_ids);
	oscap_stringlist_free(remed->group_ids);
	free(remed);
}

struct cvrf_remediation *cvrf_remediation_clone(const struct cvrf_remediation *remed) {
	struct cvrf_remediation *clone = malloc(sizeof(struct cvrf_remediation));
	clone->type = remed->type;
	clone->date = oscap_strdup(remed->date);
	clone->description = oscap_strdup(remed->description);
	clone->url = oscap_strdup(remed->url);
	clone->entitlement = oscap_strdup(remed->entitlement);
	clone->product_ids = oscap_stringlist_clone(remed->product_ids);
	clone->group_ids = oscap_stringlist_clone(remed->group_ids);
	return clone;
}

/***************************************************************************
 * CVRF Score Set
 */

struct cvrf_score_set {
	char *vector;
	struct cvss_impact *impact;
	struct oscap_stringlist *product_ids;
};
OSCAP_ACCESSOR_STRING(cvrf_score_set, vector)
OSCAP_ACCESSOR_SIMPLE(struct cvss_impact*, cvrf_score_set, impact)

struct oscap_string_iterator *cvrf_score_set_get_product_ids(struct cvrf_score_set *score_set) {
	return oscap_stringlist_get_strings(score_set->product_ids);
}

bool cvrf_score_set_add_metric(struct cvrf_score_set *score_set, enum cvss_category category, const char *score) {
	struct cvss_metrics *metric = cvss_metrics_new(category);
	cvss_metrics_set_score(metric, strtod(score, NULL));
	return cvss_impact_set_metrics(score_set->impact, metric);
}

static char *cvrf_score_set_get_score(const struct cvrf_score_set *score_set, enum cvss_category category) {
	struct cvss_metrics *metric = NULL;
	if (category == CVSS_BASE) {
		metric = cvss_impact_get_base_metrics(score_set->impact);
	} else if (category == CVSS_ENVIRONMENTAL) {
		metric = cvss_impact_get_environmental_metrics(score_set->impact);
	} else if (category == CVSS_TEMPORAL) {
		metric = cvss_impact_get_temporal_metrics(score_set->impact);
	}
	if (metric == NULL)
		return NULL;

	float score = cvss_metrics_get_score(metric);
	if (isnan(score) != 0) {
		return NULL;
	} else {
		return oscap_sprintf("%f", score);
	}
}

char *cvrf_score_set_get_base_score(const struct cvrf_score_set *score_set) {
	return cvrf_score_set_get_score(score_set, CVSS_BASE);
}

char *cvrf_score_set_get_environmental_score(const struct cvrf_score_set *score_set) {
	return cvrf_score_set_get_score(score_set, CVSS_ENVIRONMENTAL);
}

char *cvrf_score_set_get_temporal_score(const struct cvrf_score_set *score_set) {
	return cvrf_score_set_get_score(score_set, CVSS_TEMPORAL);
}

struct cvrf_score_set *cvrf_score_set_new() {
	struct cvrf_score_set *ret = malloc(sizeof(struct cvrf_score_set));
	if (ret == NULL)
		return NULL;

	ret->vector = NULL;
	ret->impact = cvss_impact_new();
	ret->product_ids = oscap_stringlist_new();
	return ret;
}

void cvrf_score_set_free(struct cvrf_score_set *score_set) {
	if (score_set == NULL)
		return;

	free(score_set->vector);
	cvss_impact_free(score_set->impact);
	oscap_stringlist_free(score_set->product_ids);
	free(score_set);
}

struct cvrf_score_set *cvrf_score_set_clone(const struct cvrf_score_set *score_set) {
	struct cvrf_score_set *clone = malloc(sizeof(struct cvrf_score_set));
	clone->vector = oscap_strdup(score_set->vector);
	clone->impact= cvss_impact_clone(score_set->impact);
	clone->product_ids = oscap_stringlist_clone(score_set->product_ids);
	return clone;
}


/***************************************************************************
 * CVRF threat
 */
struct cvrf_threat {
	cvrf_threat_type_t type;
	char *date;
	char *description;
	struct oscap_stringlist *product_ids;
	struct oscap_stringlist *group_ids;
};
OSCAP_ACCESSOR_STRING(cvrf_threat, date)
OSCAP_ACCESSOR_STRING(cvrf_threat, description)

cvrf_threat_type_t cvrf_threat_get_threat_type(struct cvrf_threat *threat) {
	return threat->type;
}
struct oscap_string_iterator *cvrf_threat_get_product_ids(struct cvrf_threat *threat) {
	return oscap_stringlist_get_strings(threat->product_ids);
}
struct oscap_string_iterator *cvrf_threat_get_group_ids(struct cvrf_threat *threat) {
	return oscap_stringlist_get_strings(threat->group_ids);
}

struct cvrf_threat *cvrf_threat_new() {
	struct cvrf_threat *ret = malloc(sizeof(struct cvrf_threat));
	if (ret == NULL)
		return NULL;

	ret->type = CVRF_THREAT_UNKNOWN;
	ret->date = NULL;
	ret->description = NULL;
	ret->product_ids = oscap_stringlist_new();
	ret->group_ids = oscap_stringlist_new();

	return ret;
}

void cvrf_threat_free(struct cvrf_threat *threat) {
	if (threat == NULL)
		return;

	free(threat->date);
	free(threat->description);
	oscap_stringlist_free(threat->product_ids);
	oscap_stringlist_free(threat->group_ids);
	free(threat);
}

struct cvrf_threat *cvrf_threat_clone(const struct cvrf_threat *threat) {
	struct cvrf_threat *clone = malloc(sizeof(struct cvrf_threat));
	clone->type = threat->type;
	clone->date = oscap_strdup(threat->date);
	clone->description = oscap_strdup(threat->description);
	clone->product_ids = oscap_stringlist_clone(threat->product_ids);
	clone->group_ids = oscap_stringlist_clone(threat->group_ids);
	return clone;
}

/***************************************************************************
 * CVRF ProductStatus
 */

struct cvrf_product_status {
	cvrf_product_status_type_t type;
	struct oscap_stringlist *product_ids;
};

struct oscap_string_iterator *cvrf_product_status_get_ids(struct cvrf_product_status *stat) {
	return oscap_stringlist_get_strings(stat->product_ids);
}
cvrf_product_status_type_t cvrf_product_status_get_type(struct cvrf_product_status *stat) {
	return stat->type;
}

struct cvrf_product_status *cvrf_product_status_new() {
	struct cvrf_product_status *ret = malloc(sizeof(struct cvrf_product_status));
	if (ret == NULL)
		return NULL;

	ret->type = CVRF_PRODUCT_STATUS_UNKNOWN;
	ret->product_ids = oscap_stringlist_new();
	return ret;
}

void cvrf_product_status_free(struct cvrf_product_status *status) {
	if (status == NULL)
		return;

	oscap_stringlist_free(status->product_ids);
	free(status);
}

struct cvrf_product_status *cvrf_product_status_clone(const struct cvrf_product_status *stat) {
	struct cvrf_product_status *clone = malloc(sizeof(struct cvrf_product_status));
	clone->type = stat->type;
	clone->product_ids = oscap_stringlist_clone(stat->product_ids);
	return clone;
}

/***************************************************************************
 * CVRF Involvement
 */

struct cvrf_involvement {
	cvrf_involvement_status_type_t status;
	cvrf_doc_publisher_type_t party;
	char *description;
};
OSCAP_ACCESSOR_STRING(cvrf_involvement, description)

cvrf_involvement_status_type_t cvrf_involvement_get_status_type(struct cvrf_involvement *involve) {
	return involve->status;
}

cvrf_doc_publisher_type_t cvrf_involvement_get_party(struct cvrf_involvement *involve) {
	return involve->party;
}

struct cvrf_involvement *cvrf_involvement_new() {
	struct cvrf_involvement *ret = malloc(sizeof(struct cvrf_involvement));
	if (ret == NULL)
		return NULL;

	ret->status = CVRF_INVOLVEMENT_UNKNOWN;
	ret->party = CVRF_DOC_PUBLISHER_UNKNOWN;
	ret->description = NULL;
	return ret;
}

void cvrf_involvement_free(struct cvrf_involvement *involve) {
	if (involve == NULL)
		return;

	free(involve->description);
	free(involve);
}

struct cvrf_involvement *cvrf_involvement_clone(const struct cvrf_involvement *involve) {
	struct cvrf_involvement *clone = malloc(sizeof(struct cvrf_involvement));
	clone->status = involve->status;
	clone->party = involve->party;
	clone->description = oscap_strdup(involve->description);
	return clone;
}

/***************************************************************************
 * CVRF Vulnerability CWE
 */

struct cvrf_vulnerability_cwe {
	char *cwe;
	char *id;
};
OSCAP_ACCESSOR_STRING(cvrf_vulnerability_cwe, cwe)
OSCAP_ACCESSOR_STRING(cvrf_vulnerability_cwe, id)

struct cvrf_vulnerability_cwe *cvrf_vulnerability_cwe_new() {
	struct cvrf_vulnerability_cwe *ret = malloc(sizeof(struct cvrf_vulnerability_cwe));
	if (ret == NULL)
		return NULL;

	ret->cwe = NULL;
	ret->id = NULL;
	return ret;
}

void cvrf_vulnerability_cwe_free(struct cvrf_vulnerability_cwe *vuln_cwe) {
	if (vuln_cwe == NULL)
		return;

	free(vuln_cwe->cwe);
	free(vuln_cwe->id);
	free(vuln_cwe);
}

struct cvrf_vulnerability_cwe *cvrf_vulnerability_cwe_clone(const struct cvrf_vulnerability_cwe *vuln_cwe) {
	struct cvrf_vulnerability_cwe *clone = malloc(sizeof(struct cvrf_vulnerability_cwe));
	clone->cwe = vuln_cwe->cwe;
	clone->id = vuln_cwe->id;
	return clone;
}

/***************************************************************************
 * CVRF Vulnerability
 */

struct cvrf_vulnerability {
	int ordinal;
	char *title;
	char *system_id;
	char *system_name;
	char *discovery_date;
	char *release_date;
	char *cve_id;
	struct oscap_list *cwes;
	struct oscap_list *notes;
	struct oscap_list *involvements;
	struct oscap_list *score_sets;
	struct oscap_list *product_statuses;
	struct oscap_list *threats;
	struct oscap_list *remediations;
	struct oscap_list *references;
	struct oscap_list *acknowledgments;

};
OSCAP_ACCESSOR_SIMPLE(int, cvrf_vulnerability, ordinal)
OSCAP_ACCESSOR_STRING(cvrf_vulnerability, title)
OSCAP_ACCESSOR_STRING(cvrf_vulnerability, system_id)
OSCAP_ACCESSOR_STRING(cvrf_vulnerability, system_name)
OSCAP_ACCESSOR_STRING(cvrf_vulnerability, discovery_date)
OSCAP_ACCESSOR_STRING(cvrf_vulnerability, release_date)
OSCAP_ACCESSOR_STRING(cvrf_vulnerability, cve_id)
OSCAP_IGETINS_GEN(cvrf_involvement, cvrf_vulnerability, involvements, involvement)
OSCAP_ITERATOR_REMOVE_F(cvrf_involvement)
OSCAP_IGETINS_GEN(cvrf_score_set, cvrf_vulnerability, score_sets, score_set)
OSCAP_ITERATOR_REMOVE_F(cvrf_score_set)
OSCAP_IGETINS_GEN(cvrf_product_status, cvrf_vulnerability, product_statuses, cvrf_product_status)
OSCAP_ITERATOR_REMOVE_F(cvrf_product_status)
OSCAP_IGETINS_GEN(cvrf_remediation, cvrf_vulnerability, remediations, remediation)
OSCAP_ITERATOR_REMOVE_F(cvrf_remediation)
OSCAP_IGETINS_GEN(cvrf_threat, cvrf_vulnerability, threats, threat)
OSCAP_ITERATOR_REMOVE_F(cvrf_threat)
OSCAP_IGETINS_GEN(cvrf_vulnerability_cwe, cvrf_vulnerability, cwes, cwe)
OSCAP_ITERATOR_REMOVE_F(cvrf_vulnerability_cwe)

struct oscap_iterator *cvrf_vulnerability_get_references(struct cvrf_vulnerability *vuln) {
	return oscap_iterator_new(vuln->references);
}

struct oscap_iterator *cvrf_vulnerability_get_acknowledgments(struct cvrf_vulnerability *vuln) {
	return oscap_iterator_new(vuln->acknowledgments);
}

struct oscap_iterator *cvrf_vulnerability_get_notes(struct cvrf_vulnerability *vuln) {
	return oscap_iterator_new(vuln->notes);
}


struct cvrf_vulnerability *cvrf_vulnerability_new() {
	struct cvrf_vulnerability *ret = malloc(sizeof(struct cvrf_vulnerability));
	if (ret == NULL)
		return NULL;

	ret->ordinal = 0;
	ret->title = NULL;
	ret->system_id = NULL;
	ret->system_name = NULL;
	ret->discovery_date = NULL;
	ret->release_date = NULL;
	ret->cve_id = NULL;
	ret->cwes = oscap_list_new();
	ret->notes = oscap_list_new();
	ret->involvements = oscap_list_new();
	ret->score_sets = oscap_list_new();
	ret->product_statuses = oscap_list_new();
	ret->threats = oscap_list_new();
	ret->remediations = oscap_list_new();
	ret->references = oscap_list_new();
	ret->acknowledgments = oscap_list_new();
	return ret;
}

void cvrf_vulnerability_free(struct cvrf_vulnerability *vulnerability) {
	if (vulnerability == NULL)
		return;

	free(vulnerability->title);
	free(vulnerability->system_id);
	free(vulnerability->system_name);
	free(vulnerability->discovery_date);
	free(vulnerability->release_date);
	free(vulnerability->cve_id);
	oscap_list_free(vulnerability->cwes, (oscap_destruct_func) cvrf_vulnerability_cwe_free);
	oscap_list_free(vulnerability->notes, (oscap_destruct_func) cvrf_note_free);
	oscap_list_free(vulnerability->involvements, (oscap_destruct_func) cvrf_involvement_free);
	oscap_list_free(vulnerability->score_sets, (oscap_destruct_func) cvrf_score_set_free);
	oscap_list_free(vulnerability->product_statuses, (oscap_destruct_func) cvrf_product_status_free);
	oscap_list_free(vulnerability->threats, (oscap_destruct_func) cvrf_threat_free);
	oscap_list_free(vulnerability->remediations, (oscap_destruct_func) cvrf_remediation_free);
	oscap_list_free(vulnerability->references, (oscap_destruct_func) cvrf_reference_free);
	oscap_list_free(vulnerability->acknowledgments, (oscap_destruct_func) cvrf_acknowledgment_free);
	free(vulnerability);
}

struct cvrf_vulnerability *cvrf_vulnerability_clone(const struct cvrf_vulnerability *vuln) {
	struct cvrf_vulnerability *clone = malloc(sizeof(struct cvrf_vulnerability));
	clone->ordinal = vuln->ordinal;
	clone->title = oscap_strdup(vuln->title);
	clone->system_id = oscap_strdup(vuln->system_id);
	clone->system_id = oscap_strdup(vuln->system_name);
	clone->discovery_date = oscap_strdup(vuln->discovery_date);
	clone->release_date = oscap_strdup(vuln->release_date);
	clone->cwes = oscap_list_clone(vuln->cwes, (oscap_clone_func) cvrf_vulnerability_cwe_clone);
	clone->notes = oscap_list_clone(vuln->notes, (oscap_clone_func) cvrf_note_clone);
	clone->involvements = oscap_list_clone(vuln->involvements, (oscap_clone_func) cvrf_involvement_clone);
	clone->product_statuses = oscap_list_clone(vuln->product_statuses, (oscap_clone_func) cvrf_product_status_clone);
	clone->threats = oscap_list_clone(vuln->threats, (oscap_clone_func) cvrf_threat_clone);
	clone->score_sets = oscap_list_clone(vuln->score_sets, (oscap_clone_func) cvrf_score_set_clone);
	clone->remediations = oscap_list_clone(vuln->remediations, (oscap_clone_func) cvrf_remediation_clone);
	clone->references = oscap_list_clone(vuln->references, (oscap_clone_func) cvrf_reference_clone);
	clone->acknowledgments = oscap_list_clone(vuln->acknowledgments, (oscap_clone_func) cvrf_acknowledgment_clone);
	return clone;
}

int cvrf_vulnerability_filter_by_product(struct cvrf_vulnerability *vuln, const char *prod) {
	struct oscap_stringlist *filtered_ids = oscap_stringlist_new();
	int ret = 0;

	struct cvrf_product_status_iterator *statuses = cvrf_vulnerability_get_product_statuses(vuln);
	while (cvrf_product_status_iterator_has_more(statuses)) {
		struct cvrf_product_status *stat = cvrf_product_status_iterator_next(statuses);

		struct oscap_string_iterator *products = cvrf_product_status_get_ids(stat);
		while (oscap_string_iterator_has_more(products)) {
			const char *product_id = oscap_string_iterator_next(products);
			if (oscap_str_startswith(product_id, prod))
				oscap_stringlist_add_string(filtered_ids, product_id);
		}
		oscap_string_iterator_free(products);

		if (oscap_list_get_itemcount((struct oscap_list *)filtered_ids) == 0) {
			oscap_stringlist_free(filtered_ids);
			ret = -1;
			break;
		} else {
			oscap_stringlist_free(stat->product_ids);
			stat->product_ids = filtered_ids;
		}
	}
	cvrf_product_status_iterator_free(statuses);
	return ret;
}



/***************************************************************************
****************************************************************************
 * Product tree offshoot of main CVRF model
 */

/***************************************************************************
 * CVRF FullProductName
 */
struct cvrf_product_name {
	char *product_id;
	char *cpe;
};
OSCAP_ACCESSOR_STRING(cvrf_product_name, product_id)
OSCAP_ACCESSOR_STRING(cvrf_product_name, cpe)

struct cvrf_product_name *cvrf_product_name_new() {
	struct cvrf_product_name *ret = malloc(sizeof(struct cvrf_product_name));
	if (ret == NULL)
		return NULL;

	ret->product_id = NULL;
	ret->cpe = NULL;
	return ret;
}

void cvrf_product_name_free(struct cvrf_product_name *full_name) {
	if (full_name == NULL)
		return;

	free(full_name->product_id);
	free(full_name->cpe);
	free(full_name);
}

struct cvrf_product_name *cvrf_product_name_clone(const struct cvrf_product_name *full_name) {
	struct cvrf_product_name *clone = malloc(sizeof(struct cvrf_product_name));
	clone->product_id = oscap_strdup(full_name->product_id);
	clone->cpe = oscap_strdup(full_name->cpe);
	return clone;
}

/***************************************************************************
 * CVRF ProductGroup
 */
struct cvrf_group {
	char *group_id;
	char *description;
	struct oscap_stringlist *product_ids;
};
OSCAP_ACCESSOR_STRING(cvrf_group, group_id)
OSCAP_ACCESSOR_STRING(cvrf_group, description)

struct oscap_string_iterator *cvrf_group_get_product_ids(struct cvrf_group *group) {
	return oscap_stringlist_get_strings(group->product_ids);
}

struct cvrf_group *cvrf_group_new() {
	struct cvrf_group *ret = malloc(sizeof(struct cvrf_group));
	if (ret == NULL)
		return NULL;

	ret->group_id = NULL;
	ret->description = NULL;
	ret->product_ids = oscap_stringlist_new();
	return ret;
}

void cvrf_group_free(struct cvrf_group *group) {
	if (group == NULL)
		return;

	free(group->group_id);
	free(group->description);
	oscap_stringlist_free(group->product_ids);
	free(group);
}

struct cvrf_group *cvrf_group_clone(const struct cvrf_group *group) {
	struct cvrf_group *clone = malloc(sizeof(struct cvrf_group));
	clone->group_id = oscap_strdup(group->group_id);
	clone->description = oscap_strdup(group->description);
	clone->product_ids = oscap_stringlist_clone(group->product_ids);
	return clone;
}

/***************************************************************************
 * CVRF Relationship
 */
struct cvrf_relationship {
	char *product_reference;
	cvrf_relationship_type_t relation_type;
	char *relates_to_ref;
	struct cvrf_product_name *product_name;
};
OSCAP_ACCESSOR_STRING(cvrf_relationship, product_reference)
OSCAP_ACCESSOR_STRING(cvrf_relationship, relates_to_ref)
OSCAP_ACCESSOR_SIMPLE(struct cvrf_product_name*, cvrf_relationship, product_name)

cvrf_relationship_type_t cvrf_relationship_get_relation_type(struct cvrf_relationship *relation) {
	return relation->relation_type;
}

struct cvrf_relationship *cvrf_relationship_new() {
	struct cvrf_relationship *ret = malloc(sizeof(struct cvrf_relationship));
	if (ret == NULL)
		return NULL;

	ret->product_reference = NULL;
	ret->relation_type = CVRF_RELATIONSHIP_UNKNOWN;
	ret->relates_to_ref = NULL;
	ret->product_name = cvrf_product_name_new();

	return ret;
}

void cvrf_relationship_free(struct cvrf_relationship *relationship) {
	if (relationship == NULL)
		return;

	free(relationship->product_reference);
	free(relationship->relates_to_ref);
	cvrf_product_name_free(relationship->product_name);
	free(relationship);
}

struct cvrf_relationship *cvrf_relationship_clone(const struct cvrf_relationship *relation) {
	struct cvrf_relationship *clone = malloc(sizeof(struct cvrf_relationship));
	clone->relation_type = relation->relation_type;
	clone->product_reference = oscap_strdup(relation->product_reference);
	clone->relates_to_ref = oscap_strdup(relation->relates_to_ref);
	clone->product_name = cvrf_product_name_clone(relation->product_name);
	return clone;
}

/***************************************************************************
 * CVRF Branch
 */
struct cvrf_branch {
	cvrf_branch_type_t type;
	char *branch_name;
	struct cvrf_product_name *product_name;
	struct oscap_list *subbranches;
};
OSCAP_ACCESSOR_STRING(cvrf_branch, branch_name)
OSCAP_ACCESSOR_SIMPLE(struct cvrf_product_name*, cvrf_branch, product_name)

struct oscap_iterator *cvrf_branch_get_subbranches(struct cvrf_branch *branch) {
	return oscap_iterator_new(branch->subbranches);
}

cvrf_branch_type_t cvrf_branch_get_branch_type(struct cvrf_branch *branch) {
	return branch->type;
}

struct cvrf_branch *cvrf_branch_new() {
	struct cvrf_branch *ret = malloc(sizeof(struct cvrf_branch));
	if (ret == NULL)
		return NULL;

	ret->type = CVRF_BRANCH_UNKNOWN;
	ret->branch_name = NULL;
	ret->product_name = cvrf_product_name_new();
	ret->subbranches = oscap_list_new();
	return ret;
}

void cvrf_branch_free(struct cvrf_branch *branch) {
	if (branch == NULL)
		return;

	free(branch->branch_name);
	cvrf_product_name_free(branch->product_name);
	oscap_list_free(branch->subbranches, (oscap_destruct_func) cvrf_branch_free);
	free(branch);
}

struct cvrf_branch *cvrf_branch_clone(const struct cvrf_branch *branch) {
	struct cvrf_branch *clone = malloc(sizeof(struct cvrf_branch));
	clone->branch_name = oscap_strdup(branch->branch_name);
	clone->type = branch->type;
	clone->product_name = cvrf_product_name_clone(branch->product_name);
	clone->subbranches = oscap_list_clone(branch->subbranches, (oscap_clone_func) cvrf_branch_clone);
	return clone;
}

static const char *get_cvrf_product_id_from_branch(struct cvrf_branch *branch, const char *cpe) {
	const char *product_id = NULL;
	if (cvrf_branch_get_branch_type(branch) == CVRF_BRANCH_PRODUCT_FAMILY) {
		struct oscap_iterator *subbranches = cvrf_branch_get_subbranches(branch);
		while(oscap_iterator_has_more(subbranches) && product_id == NULL) {
			product_id = get_cvrf_product_id_from_branch(oscap_iterator_next(subbranches), cpe);
		}
		oscap_iterator_free(subbranches);
	}
	else {
		if (!strcmp(cvrf_branch_get_branch_name(branch), cpe))
			return cvrf_product_name_get_product_id(branch->product_name);
	}
	return product_id;
}

/***************************************************************************
 * CVRF ProductTree
 */

struct cvrf_product_tree {
	struct oscap_list *product_names;
	struct oscap_list *branches;
	struct oscap_list *relationships;
	struct oscap_list *product_groups;
};
OSCAP_IGETINS_GEN(cvrf_product_name, cvrf_product_tree, product_names, product_name)
OSCAP_ITERATOR_REMOVE_F(cvrf_product_name)
OSCAP_IGETINS_GEN(cvrf_relationship, cvrf_product_tree, relationships, relationship)
OSCAP_ITERATOR_REMOVE_F(cvrf_relationship)
OSCAP_IGETINS_GEN(cvrf_group, cvrf_product_tree, product_groups, group)
OSCAP_ITERATOR_REMOVE_F(cvrf_group)

struct oscap_iterator *cvrf_product_tree_get_branches(struct cvrf_product_tree *tree) {
	return oscap_iterator_new(tree->branches);
}

struct cvrf_product_tree *cvrf_product_tree_new() {
	struct cvrf_product_tree *ret = malloc(sizeof(struct cvrf_product_tree));
	if (ret == NULL)
		return NULL;

	ret->product_names = oscap_list_new();
	ret->branches = oscap_list_new();
	ret->relationships = oscap_list_new();
	ret->product_groups = oscap_list_new();
	return ret;
}

void cvrf_product_tree_free(struct cvrf_product_tree *tree) {
	if (tree == NULL)
		return;

	oscap_list_free(tree->product_names, (oscap_destruct_func) cvrf_product_name_free);
	oscap_list_free(tree->branches, (oscap_destruct_func) cvrf_branch_free);
	oscap_list_free(tree->relationships, (oscap_destruct_func) cvrf_relationship_free);
	oscap_list_free(tree->product_groups, (oscap_destruct_func) cvrf_group_free);
	free(tree);
}

struct cvrf_product_tree *cvrf_product_tree_clone(const struct cvrf_product_tree *tree) {
	struct cvrf_product_tree *clone = malloc(sizeof(struct cvrf_product_tree));
	clone->product_names = oscap_list_clone(tree->product_names, (oscap_clone_func) cvrf_product_name_clone);
	clone->branches = oscap_list_clone(tree->branches, (oscap_clone_func) cvrf_branch_clone);
	clone->relationships = oscap_list_clone(tree->relationships, (oscap_clone_func) cvrf_relationship_clone);
	clone->product_groups = oscap_list_clone(tree->product_groups, (oscap_clone_func) cvrf_group_clone);
	return clone;
}

const char *get_cvrf_product_id_from_cpe(struct cvrf_product_tree *tree, const char *cpe) {
	const char *branch_id = NULL;
	struct oscap_iterator *branches = cvrf_product_tree_get_branches(tree);
	while (oscap_iterator_has_more(branches) && branch_id == NULL) {
		branch_id = get_cvrf_product_id_from_branch(oscap_iterator_next(branches), cpe);
	}
	oscap_iterator_free(branches);

	return branch_id;
}

int cvrf_product_tree_filter_by_cpe(struct cvrf_product_tree *tree, const char *cpe) {
	const char *branch_id = get_cvrf_product_id_from_cpe(tree, cpe);
	if (branch_id == NULL)
		return -1;

	struct oscap_list *filtered_relation = oscap_list_new();
	struct cvrf_relationship_iterator *relationships = cvrf_product_tree_get_relationships(tree);
	while (cvrf_relationship_iterator_has_more(relationships)) {
		struct cvrf_relationship *relation = cvrf_relationship_iterator_next(relationships);
		if (!strcmp(branch_id, cvrf_relationship_get_relates_to_ref(relation)))
			oscap_list_add(filtered_relation, cvrf_relationship_clone(relation));
	}
	cvrf_relationship_iterator_free(relationships);

	if (oscap_list_get_itemcount(filtered_relation) == 0) {
		oscap_list_free(filtered_relation, (oscap_destruct_func) cvrf_relationship_free);
		return -1;
	} else {
		oscap_list_free(tree->relationships, (oscap_destruct_func) cvrf_relationship_free);
		tree->relationships = filtered_relation;
		return 0;
	}
}

/***************************************************************************
 * CVRF Acknowledgments
 */

struct cvrf_acknowledgment {
	struct oscap_stringlist *names;
	struct oscap_stringlist *organizations;
	char *description;
	struct oscap_stringlist *urls;
};
OSCAP_ACCESSOR_STRING(cvrf_acknowledgment, description)

struct oscap_string_iterator *cvrf_acknowledgment_get_names(const struct cvrf_acknowledgment *ack) {
	return oscap_stringlist_get_strings(ack->names);
}

struct oscap_string_iterator *cvrf_acknowledgment_get_organizations(const struct cvrf_acknowledgment *ack) {
	return oscap_stringlist_get_strings(ack->organizations);
}

struct oscap_string_iterator *cvrf_acknowledgment_get_urls(const struct cvrf_acknowledgment *ack) {
	return oscap_stringlist_get_strings(ack->urls);
}

struct cvrf_acknowledgment *cvrf_acknowledgment_new() {
	struct cvrf_acknowledgment *ret = malloc(sizeof(struct cvrf_acknowledgment));
	if (ret == NULL)
		return NULL;

	ret->names = oscap_stringlist_new();
	ret->organizations = oscap_stringlist_new();
	ret->description = NULL;
	ret->urls = oscap_stringlist_new();
	return ret;
}

void cvrf_acknowledgment_free(struct cvrf_acknowledgment *ack) {
	if (ack == NULL)
		return;

	oscap_stringlist_free(ack->names);
	oscap_stringlist_free(ack->organizations);
	free(ack->description);
	oscap_stringlist_free(ack->urls);
	free(ack);
}

struct cvrf_acknowledgment *cvrf_acknowledgment_clone(const struct cvrf_acknowledgment *ack) {
	struct cvrf_acknowledgment *clone = malloc(sizeof(struct cvrf_acknowledgment));
	clone->names = oscap_stringlist_clone(ack->names);
	clone->organizations = oscap_stringlist_clone(ack->organizations);
	clone->description = oscap_strdup(ack->description);
	clone->urls = oscap_stringlist_clone(ack->urls);
	return clone;
}

/***************************************************************************
 * CVRF Notes
 */
struct cvrf_note {
	cvrf_note_type_t type;
	int ordinal;
	char *audience;
	char *title;
	char *contents;
};
OSCAP_ACCESSOR_SIMPLE(int, cvrf_note, ordinal)
OSCAP_ACCESSOR_STRING(cvrf_note, audience)
OSCAP_ACCESSOR_STRING(cvrf_note, title)
OSCAP_ACCESSOR_STRING(cvrf_note, contents)

cvrf_note_type_t cvrf_note_get_note_type(const struct cvrf_note *note) {
	return note->type;
}

struct cvrf_note *cvrf_note_new() {
	struct cvrf_note *ret = malloc(sizeof(struct cvrf_note));
	if (ret == NULL)
		return NULL;

	ret->type = CVRF_NOTE_UNKNOWN;
	ret->ordinal = 0;
	ret->audience = NULL;
	ret->title = NULL;
	ret->contents = NULL;
	return ret;
}

void cvrf_note_free(struct cvrf_note *note) {
	if (note == NULL)
		return;

	free(note->audience);
	free(note->title);
	free(note->contents);
	free(note);
}

struct cvrf_note *cvrf_note_clone(const struct cvrf_note *note) {
	struct cvrf_note *clone = malloc(sizeof(struct cvrf_note));
	clone->type = note->type;
	clone->ordinal = note->ordinal;
	clone->audience = oscap_strdup(note->audience);
	clone->title = oscap_strdup(note->title);
	clone->contents = oscap_strdup(note->contents);
	return clone;
}

/***************************************************************************
 * CVRF Revision
 */

struct cvrf_revision {
	char *number;
	char *date;
	char *description;
};
OSCAP_ACCESSOR_STRING(cvrf_revision, number)
OSCAP_ACCESSOR_STRING(cvrf_revision, date)
OSCAP_ACCESSOR_STRING(cvrf_revision, description)

struct cvrf_revision *cvrf_revision_new() {
	struct cvrf_revision *ret = malloc(sizeof(struct cvrf_revision));
	if (ret == NULL)
		return NULL;

	ret->number = NULL;
	ret->date = NULL;
	ret->description = NULL;
	return ret;
}

void cvrf_revision_free(struct cvrf_revision *revision) {
	if (revision == NULL)
		return;

	free(revision->number);
	free(revision->date);
	free(revision->description);
	free(revision);
}

struct cvrf_revision *cvrf_revision_clone(const struct cvrf_revision *revision) {
	struct cvrf_revision *clone = malloc(sizeof(struct cvrf_revision));
	clone->number = oscap_strdup(revision->number);
	clone->date = oscap_strdup(revision->date);
	clone->description = oscap_strdup(revision->description);
	return clone;
}

/***************************************************************************
 * CVRF DocumentTracking
 */
struct cvrf_doc_tracking {
	char *tracking_id;
	struct oscap_stringlist *aliases;
	cvrf_doc_status_type_t status;
	char *version;
	struct oscap_list *revision_history;
	char *init_release_date;
	char *cur_release_date;
	// Generator
	char *generator_engine;
	char *generator_date;
};
OSCAP_ACCESSOR_STRING(cvrf_doc_tracking, tracking_id)
OSCAP_ACCESSOR_STRING(cvrf_doc_tracking, version)
OSCAP_IGETINS_GEN(cvrf_revision, cvrf_doc_tracking, revision_history, revision)
OSCAP_ITERATOR_REMOVE_F(cvrf_revision)
OSCAP_ACCESSOR_STRING(cvrf_doc_tracking, init_release_date)
OSCAP_ACCESSOR_STRING(cvrf_doc_tracking, cur_release_date)
OSCAP_ACCESSOR_STRING(cvrf_doc_tracking, generator_engine)
OSCAP_ACCESSOR_STRING(cvrf_doc_tracking, generator_date)

cvrf_doc_status_type_t cvrf_doc_tracking_get_status(struct cvrf_doc_tracking *tracking) {
	return tracking->status;
}
struct oscap_string_iterator *cvrf_doc_tracking_get_aliases(struct cvrf_doc_tracking *tracking) {
	return oscap_stringlist_get_strings(tracking->aliases);
}

struct cvrf_doc_tracking *cvrf_doc_tracking_new() {
	struct cvrf_doc_tracking *ret = malloc(sizeof(struct cvrf_doc_tracking));
	if (ret == NULL)
		return NULL;

	ret->tracking_id = NULL;
	ret->aliases = oscap_stringlist_new();
	ret->status = CVRF_DOC_STATUS_UNKNOWN;
	ret->version = NULL;
	ret->revision_history = oscap_list_new();
	ret->init_release_date = NULL;
	ret->cur_release_date = NULL;
	ret->generator_engine = NULL;
	ret->generator_date = NULL;
	return ret;
}

void cvrf_doc_tracking_free(struct cvrf_doc_tracking *tracking) {
	if (tracking == NULL)
		return;

	free(tracking->tracking_id);
	oscap_stringlist_free(tracking->aliases);
	free(tracking->version);
	oscap_list_free(tracking->revision_history, (oscap_destruct_func) cvrf_revision_free);
	free(tracking->init_release_date);
	free(tracking->cur_release_date);
	free(tracking->generator_engine);
	free(tracking->generator_date);
	free(tracking);
}

struct cvrf_doc_tracking *cvrf_doc_tracking_clone(const struct cvrf_doc_tracking *tracking) {
	struct cvrf_doc_tracking *clone = malloc(sizeof(struct cvrf_doc_tracking));
	clone->tracking_id = oscap_strdup(tracking->tracking_id);
	clone->aliases = oscap_stringlist_clone(tracking->aliases);
	clone->status = tracking->status;
	clone->version = oscap_strdup(tracking->version);
	clone->revision_history = oscap_list_clone(tracking->revision_history, (oscap_clone_func) cvrf_revision_clone);
	clone->init_release_date = oscap_strdup(tracking->init_release_date);
	clone->cur_release_date = oscap_strdup(tracking->cur_release_date);
	clone->generator_engine = oscap_strdup(tracking->generator_engine);
	clone->generator_date = oscap_strdup(tracking->generator_date);
	return clone;
}

/***************************************************************************
 * CVRF DocumentPublisher
 */

struct cvrf_doc_publisher {
	cvrf_doc_publisher_type_t type;
	char *vendor_id;
	char *contact_details;
	char *issuing_authority;
};
OSCAP_ACCESSOR_STRING(cvrf_doc_publisher, vendor_id)
OSCAP_ACCESSOR_STRING(cvrf_doc_publisher, contact_details)
OSCAP_ACCESSOR_STRING(cvrf_doc_publisher, issuing_authority)

cvrf_doc_publisher_type_t cvrf_doc_publisher_get_type(struct cvrf_doc_publisher *publisher) {
	return publisher->type;
}

struct cvrf_doc_publisher *cvrf_doc_publisher_new() {
	struct cvrf_doc_publisher *ret = malloc(sizeof(struct cvrf_doc_publisher));
	if (ret == NULL)
		return NULL;

	ret->type = CVRF_DOC_PUBLISHER_UNKNOWN;
	ret->vendor_id = NULL;
	ret->contact_details = NULL;
	ret->issuing_authority = NULL;
	return ret;
}

void cvrf_doc_publisher_free(struct cvrf_doc_publisher *publisher) {
	if (publisher == NULL)
		return;

	free(publisher->vendor_id);
	free(publisher->contact_details);
	free(publisher->issuing_authority);
	free(publisher);
}

struct cvrf_doc_publisher *cvrf_doc_publisher_clone(const struct cvrf_doc_publisher *publisher) {
	struct cvrf_doc_publisher *clone = malloc(sizeof(struct cvrf_doc_publisher));
	clone->type = publisher->type;
	clone->vendor_id = oscap_strdup(publisher->vendor_id);
	clone->contact_details = oscap_strdup(publisher->contact_details);
	clone->issuing_authority = oscap_strdup(publisher->issuing_authority);
	return clone;
}


/***************************************************************************
 * CVRF References
 */

struct cvrf_reference {
	cvrf_reference_type_t type;
	char *url;
	char *description;
};
OSCAP_ACCESSOR_STRING(cvrf_reference, url)
OSCAP_ACCESSOR_STRING(cvrf_reference, description)

cvrf_reference_type_t cvrf_reference_get_reference_type(struct cvrf_reference *reference) {
	return reference->type;
}

struct cvrf_reference *cvrf_reference_new() {
	struct cvrf_reference *ret = malloc(sizeof(struct cvrf_reference));
	if (ret == NULL)
		return NULL;

	ret->type = CVRF_REFERENCE_UNKNOWN;
	ret->url = NULL;
	ret->description = NULL;

	return ret;
}

void cvrf_reference_free(struct cvrf_reference *ref) {
	if (ref == NULL)
		return;

	free(ref->url);
	free(ref->description);
	free(ref);
}

struct cvrf_reference *cvrf_reference_clone(const struct cvrf_reference *ref) {
	struct cvrf_reference *clone = malloc(sizeof(struct cvrf_reference));
	clone->type = ref->type;
	clone->url = oscap_strdup(ref->url);
	clone->description = oscap_strdup(ref->description);
	return clone;
}

/***************************************************************************
 * CVRF Document
 */
struct cvrf_document {
	char *doc_distribution;
	char *aggregate_severity;
	char *namespace;
	struct cvrf_doc_tracking *tracking;
	struct cvrf_doc_publisher *publisher;
	struct oscap_list *doc_notes;
	struct oscap_list *doc_references;
	struct oscap_list *acknowledgments;
};
OSCAP_ACCESSOR_STRING(cvrf_document, doc_distribution)
OSCAP_ACCESSOR_STRING(cvrf_document, aggregate_severity)
OSCAP_ACCESSOR_STRING(cvrf_document, namespace)
OSCAP_ACCESSOR_SIMPLE(struct cvrf_doc_tracking*, cvrf_document, tracking)
OSCAP_ACCESSOR_SIMPLE(struct cvrf_doc_publisher*, cvrf_document, publisher)

struct oscap_iterator *cvrf_document_get_notes(struct cvrf_document *doc) {
	return oscap_iterator_new(doc->doc_notes);
}

struct oscap_iterator *cvrf_document_get_references(struct cvrf_document *doc) {
	return oscap_iterator_new(doc->doc_references);
}

struct oscap_iterator *cvrf_document_get_acknowledgments(struct cvrf_document *doc) {
	return oscap_iterator_new(doc->acknowledgments);
}

struct cvrf_document *cvrf_document_new() {
	struct cvrf_document *ret = malloc(sizeof(struct cvrf_document));
	if (ret == NULL)
		return NULL;

	ret->doc_distribution = NULL;
	ret->aggregate_severity = NULL;
	ret->namespace = NULL;
	ret->tracking = cvrf_doc_tracking_new();
	ret->publisher = cvrf_doc_publisher_new();
	ret->doc_notes = oscap_list_new();
	ret->doc_references = oscap_list_new();
	ret->acknowledgments = oscap_list_new();
	return ret;
}

void cvrf_document_free(struct cvrf_document *doc) {
	if (doc == NULL)
		return;

	free(doc->doc_distribution);
	free(doc->aggregate_severity);
	free(doc->namespace);
	cvrf_doc_tracking_free(doc->tracking);
	cvrf_doc_publisher_free(doc->publisher);
	oscap_list_free(doc->doc_notes, (oscap_destruct_func) cvrf_note_free);
	oscap_list_free(doc->doc_references, (oscap_destruct_func) cvrf_reference_free);
	oscap_list_free(doc->acknowledgments, (oscap_destruct_func) cvrf_acknowledgment_free);
	free(doc);
}

struct cvrf_document *cvrf_document_clone(const struct cvrf_document *doc) {
	struct cvrf_document *clone = malloc(sizeof(struct cvrf_document));
	clone->doc_distribution = oscap_strdup(doc->doc_distribution);
	clone->aggregate_severity = oscap_strdup(doc->aggregate_severity);
	clone->namespace = oscap_strdup(doc->namespace);
	clone->tracking = cvrf_doc_tracking_clone(doc->tracking);
	clone->publisher = cvrf_doc_publisher_clone(doc->publisher);
	clone->doc_notes = oscap_list_clone(doc->doc_notes, (oscap_clone_func) cvrf_note_clone);
	clone->doc_references = oscap_list_clone(doc->doc_references, (oscap_clone_func) cvrf_reference_clone);
	clone->acknowledgments = oscap_list_clone(doc->acknowledgments, (oscap_clone_func) cvrf_acknowledgment_clone);
	return clone;
}

/***************************************************************************
 * CVRF Model
 * Top-level structure of the CVRF hierarchy
 */
struct cvrf_model {
	char *doc_title;
	char *doc_type;
	struct cvrf_document *document;
	struct cvrf_product_tree *tree;
	struct oscap_list *vulnerabilities;	/* 1-n */
};
OSCAP_ACCESSOR_STRING(cvrf_model, doc_title)
OSCAP_ACCESSOR_STRING(cvrf_model, doc_type)
OSCAP_ACCESSOR_SIMPLE(struct cvrf_document*, cvrf_model, document)
OSCAP_IGETINS_GEN(cvrf_vulnerability, cvrf_model, vulnerabilities, vulnerability)
OSCAP_ITERATOR_REMOVE_F(cvrf_vulnerability)

struct cvrf_product_tree *cvrf_model_get_product_tree(struct cvrf_model *model) {
	return model->tree;
}

const char *cvrf_model_get_identification(struct cvrf_model *model) {
	struct cvrf_doc_tracking *tracking = cvrf_document_get_tracking(model->document);
	return (cvrf_doc_tracking_get_tracking_id(tracking));
}

struct cvrf_model *cvrf_model_new() {
	struct cvrf_model *ret = malloc(sizeof(struct cvrf_model));
	if (ret == NULL)
		return NULL;

	ret->doc_title = NULL;
	ret->doc_type = NULL;
	ret->document = cvrf_document_new();
	ret->tree = cvrf_product_tree_new();
	ret->vulnerabilities = oscap_list_new();
	return ret;
}

void cvrf_model_free(struct cvrf_model *cvrf) {
	if (cvrf == NULL)
		return;

	free(cvrf->doc_title);
	free(cvrf->doc_type);
	cvrf_document_free(cvrf->document);
	cvrf_product_tree_free(cvrf->tree);
	oscap_list_free(cvrf->vulnerabilities, (oscap_destruct_func) cvrf_vulnerability_free);
	free(cvrf);
}

struct cvrf_model *cvrf_model_clone(const struct cvrf_model *model) {
	struct cvrf_model *clone = malloc(sizeof(struct cvrf_model));
	clone->doc_title = oscap_strdup(model->doc_title);
	clone->doc_type = oscap_strdup(model->doc_type);
	clone->document = cvrf_document_clone(model->document);
	clone->tree = cvrf_product_tree_clone(model->tree);
	clone->vulnerabilities = oscap_list_clone(model->vulnerabilities, (oscap_clone_func) cvrf_vulnerability_clone);
	return clone;
}

int cvrf_model_filter_by_cpe(struct cvrf_model *model, const char *cpe) {
	const char *product = get_cvrf_product_id_from_cpe(model->tree, cpe);
	if (cvrf_product_tree_filter_by_cpe(model->tree, cpe) == -1)
		return -1;

	struct cvrf_vulnerability_iterator *it = cvrf_model_get_vulnerabilities(model);
	while (cvrf_vulnerability_iterator_has_more(it)) {
		cvrf_vulnerability_filter_by_product(cvrf_vulnerability_iterator_next(it), product);
	}
	cvrf_vulnerability_iterator_free(it);
	return 0;
}

/***************************************************************************
 * CVRF Index
 */

struct cvrf_index {
	char *source_url;
	char *index_file;
	struct oscap_list *models;
};
OSCAP_ACCESSOR_STRING(cvrf_index, source_url)
OSCAP_ACCESSOR_STRING(cvrf_index, index_file)
OSCAP_IGETINS_GEN(cvrf_model, cvrf_index, models, model)
OSCAP_ITERATOR_REMOVE_F(cvrf_model)

struct cvrf_index *cvrf_index_new() {
	struct cvrf_index *ret = malloc(sizeof(struct cvrf_index));
	if (ret == NULL)
		return NULL;

	ret->source_url = NULL;
	ret->index_file = NULL;
	ret->models = oscap_list_new();
	return ret;
}

void cvrf_index_free(struct cvrf_index *index) {
	if (index == NULL)
		return;

	free(index->source_url);
	free(index->index_file);
	oscap_list_free(index->models, (oscap_destruct_func) cvrf_model_free);
	free(index);
}

struct cvrf_index *cvrf_index_clone(const struct cvrf_index *index) {
	struct cvrf_index *clone = malloc(sizeof(struct cvrf_index));
	clone->source_url = oscap_strdup(index->source_url);
	clone->index_file = oscap_strdup(index->index_file);
	clone->models = oscap_list_clone(index->models, (oscap_clone_func) cvrf_model_clone);
	return clone;
}

/* End of CVRF structure definitions
 ***************************************************************************/


/*---------------------------------------------------------------------------------*\
|							XML String Variable Definitions							|
\*---------------------------------------------------------------------------------*/

#define TAG_CVRF_DOC BAD_CAST "cvrfdoc"
#define TAG_DOC_TITLE BAD_CAST "DocumentTitle"
#define TAG_DOC_TYPE BAD_CAST "DocumentType"
#define ATTR_LANG BAD_CAST "xml:lang"
#define TAG_DISTRIBUTION BAD_CAST "DocumentDistribution"
#define TAG_AGGREGATE_SEVERITY BAD_CAST "AggregateSeverity"
#define ATTR_NAMESPACE BAD_CAST "Namespace"
// DocumentPublisher
#define TAG_PUBLISHER BAD_CAST "DocumentPublisher"
#define ATTR_VENDOR_ID BAD_CAST "VendorID"
#define TAG_CONTACT_DETAILS BAD_CAST "ContactDetails"
#define TAG_ISSUING_AUTHORITY BAD_CAST "IssuingAuthority"
//Document
#define TAG_DOCUMENT_TRACKING BAD_CAST "DocumentTracking"
#define TAG_IDENTIFICATION BAD_CAST "Identification"
#define TAG_ALIAS BAD_CAST "Alias"
#define TAG_REVISION_HISTORY BAD_CAST "RevisionHistory"
#define TAG_REVISION BAD_CAST "Revision"
#define TAG_GENERATOR BAD_CAST "Generator"
#define TAG_GENERATOR_ENGINE BAD_CAST "Engine"
// Reference
#define TAG_DOCUMENT_REFERENCES BAD_CAST "DocumentReferences"
#define TAG_REFERENCES BAD_CAST "References"
#define TAG_REFERENCE BAD_CAST "Reference"
// Acknowledgment
#define TAG_ACKNOWLEDGMENTS BAD_CAST "Acknowledgments"
#define TAG_ACKNOWLEDGMENT BAD_CAST "Acknowledgment"
// Product Tree
#define TAG_PRODUCT_TREE BAD_CAST "ProductTree"
#define TAG_BRANCH BAD_CAST "Branch"
#define TAG_PRODUCT_NAME BAD_CAST "FullProductName"
//Relationship
#define TAG_RELATIONSHIP BAD_CAST "Relationship"
#define ATTR_PRODUCT_REFERENCE BAD_CAST "ProductReference"
#define ATTR_RELATES_TO_REF BAD_CAST "RelatesToProductReference"
// Group
#define TAG_PRODUCT_GROUPS BAD_CAST "ProductGroups"
#define TAG_GROUP BAD_CAST "Group"
// Vulnerabilities
#define TAG_VULNERABILITY BAD_CAST "Vulnerability"
#define ATTR_ORDINAL BAD_CAST "Ordinal"
#define TAG_DISCOVERY_DATE BAD_CAST "DiscoveryDate"
#define TAG_RELEASE_DATE BAD_CAST "ReleaseDate"
#define TAG_VULNERABILITY_CVE BAD_CAST "CVE"
#define TAG_VULNERABILITY_CWE BAD_CAST "CWE"
#define TAG_PRODUCT_STATUSES BAD_CAST "ProductStatuses"
#define TAG_INVOLVEMENTS BAD_CAST "Involvements"
#define TAG_INVOLVEMENT BAD_CAST "Involvement"
// ScoreSets
#define TAG_CVSS_SCORE_SETS BAD_CAST "CVSSScoreSets"
#define TAG_SCORE_SET BAD_CAST "ScoreSet"
#define TAG_VECTOR BAD_CAST "Vector"
#define TAG_BASE_SCORE BAD_CAST "BaseScore"
#define TAG_ENVIRONMENTAL_SCORE BAD_CAST "EnvironmentalScore"
#define TAG_TEMPORAL_SCORE BAD_CAST "TemporalScore"
// Remediations
#define TAG_REMEDIATIONS BAD_CAST "Remediations"
#define TAG_REMEDIATION BAD_CAST "Remediation"
// Threats
#define TAG_THREATS BAD_CAST "Threats"
#define TAG_THREAT BAD_CAST "Threat"
// General tags
#define TAG_DATE BAD_CAST "Date"
#define TAG_DESCRIPTION BAD_CAST "Description"
#define TAG_GROUP_ID BAD_CAST "GroupID"
#define TAG_ID BAD_CAST "ID"
#define TAG_NAME BAD_CAST "Name"
#define TAG_NUMBER BAD_CAST "Number"
#define TAG_ORGANIZATION BAD_CAST "Organization"
#define TAG_PRODUCT_ID BAD_CAST "ProductID"
#define TAG_STATUS BAD_CAST "Status"
#define TAG_TITLE BAD_CAST "Title"
#define ATTR_TYPE BAD_CAST "Type"
#define TAG_URL BAD_CAST "URL"
#define TAG_VERSION BAD_CAST "Version"

/*---------------------------------------------------------------------------------*\
|							CVRF Parsing Helper Functions							|
\*---------------------------------------------------------------------------------*/

static void cvrf_set_parsing_error(const char *element) {
	oscap_seterr(OSCAP_EFAMILY_XML, "Could not parse CVRF file: Missing or invalid"
		"%s element\n", element);
}

static void cvrf_parse_container(xmlTextReaderPtr reader, struct oscap_list *list) {
	cvrf_item_type_t item_type = cvrf_item_type_from_text((char *)xmlTextReaderConstLocalName(reader));
	const char *tag = cvrf_item_type_get_text(item_type);
	if (item_type != CVRF_VULNERABILITY && item_type != CVRF_VULNERABILITY_CWE)
		xmlTextReaderNextElement(reader);
	bool error = false;
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), BAD_CAST tag) == 0) {
		if (item_type == CVRF_REVISION) {
			error = !oscap_list_add(list, cvrf_revision_parse(reader));
		} else if (item_type == CVRF_NOTE || item_type == CVRF_DOCUMENT_NOTE) {
			error = !oscap_list_add(list, cvrf_note_parse(reader));
		} else if (item_type == CVRF_REFERENCE || item_type == CVRF_DOCUMENT_REFERENCE) {
			error = !oscap_list_add(list, cvrf_reference_parse(reader));
		} else if (item_type == CVRF_ACKNOWLEDGMENT) {
			error = !oscap_list_add(list, cvrf_acknowledgment_parse(reader));
		} else if (item_type == CVRF_GROUP) {
			error = !oscap_list_add(list, cvrf_group_parse(reader));
		} else if (item_type == CVRF_INVOLVEMENT) {
			error = !oscap_list_add(list, cvrf_involvement_parse(reader));
		} else if (item_type == CVRF_PRODUCT_STATUS) {
			error = !oscap_list_add(list, cvrf_product_status_parse(reader));
		} else if (item_type ==  CVRF_REMEDIATION) {
			error = !oscap_list_add(list, cvrf_remediation_parse(reader));
		} else if (item_type == CVRF_THREAT) {
			error = !oscap_list_add(list, cvrf_threat_parse(reader));
		} else if (item_type == CVRF_SCORE_SET) {
			error = !oscap_list_add(list, cvrf_score_set_parse(reader));
		} else if (item_type == CVRF_VULNERABILITY) {
			error = !oscap_list_add(list, cvrf_vulnerability_parse(reader));
		} else if (item_type == CVRF_VULNERABILITY_CWE) {
			error = !oscap_list_add(list, cvrf_vulnerability_cwe_parse(reader));
		}

		xmlTextReaderNextNode(reader);
		if (error) {
			cvrf_set_parsing_error(tag);
			break;
		}
	}
}

static char *cvrf_parse_element(xmlTextReaderPtr reader, const char *tagname, bool next_elm) {
	char *elm_value = NULL;
	if (xmlStrcmp(xmlTextReaderConstLocalName(reader), BAD_CAST tagname) == 0) {
		elm_value = oscap_element_string_copy(reader);
		if (next_elm)
			xmlTextReaderNextElement(reader);
	}
	return elm_value;
}

static int cvrf_parse_ordinal(xmlTextReaderPtr reader) {
	char *attribute = (char *)xmlTextReaderGetAttribute(reader, ATTR_ORDINAL);
	int ordinal = strtol(attribute, NULL, 10);
	free(attribute);
	return ordinal;
}

/*-------------------------------------------------------------------------------------*\
|							CVRF Serialization Helper Functions							|
\*-------------------------------------------------------------------------------------*/

static xmlNode *cvrf_list_to_dom(struct oscap_list *list, xmlNode *parent, cvrf_item_type_t cvrf_type) {
	if (oscap_list_get_itemcount(list) == 0)
		return NULL;

	if (parent == NULL) {
		const char *container_tag = cvrf_item_type_get_container(cvrf_type);
		parent = xmlNewNode(NULL, BAD_CAST container_tag);
	}
	xmlNode *child = NULL;
	struct oscap_iterator *it = oscap_iterator_new(list);
	while (oscap_iterator_has_more(it)) {
		if (cvrf_type == CVRF_REVISION) {
			child = cvrf_revision_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_NOTE || cvrf_type == CVRF_DOCUMENT_NOTE) {
			child = cvrf_note_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_REFERENCE || cvrf_type == CVRF_DOCUMENT_REFERENCE) {
			child = cvrf_reference_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_ACKNOWLEDGMENT) {
			child = cvrf_acknowledgment_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_PRODUCT_NAME) {
			child = cvrf_product_name_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_BRANCH) {
			child = cvrf_branch_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_RELATIONSHIP) {
			child = cvrf_relationship_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_GROUP) {
			child = cvrf_group_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_VULNERABILITY) {
			child = cvrf_vulnerability_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_VULNERABILITY_CWE) {
			child = cvrf_vulnerability_cwe_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_INVOLVEMENT) {
			child = cvrf_involvement_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_PRODUCT_STATUS) {
			child = cvrf_product_status_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_THREAT) {
			child = cvrf_threat_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_SCORE_SET) {
			child = cvrf_score_set_to_dom(oscap_iterator_next(it));
		} else if (cvrf_type == CVRF_REMEDIATION) {
			child = cvrf_remediation_to_dom(oscap_iterator_next(it));
		}
		xmlAddChild(parent, child);
	}
	oscap_iterator_free(it);
	return parent;
}

void cvrf_element_add_container(struct oscap_list *list, cvrf_item_type_t cvrf_type, xmlNode *parent) {
	xmlNode *container = cvrf_list_to_dom(list, NULL, cvrf_type);
	if (container)
		xmlAddChild(parent, container);
}

void cvrf_element_add_stringlist(struct oscap_stringlist *list, const char *tag_name, xmlNode *parent) {
	if (oscap_list_get_itemcount((struct oscap_list *)list) == 0)
		return;

	struct oscap_string_iterator *iterator = oscap_stringlist_get_strings(list);
	while (oscap_string_iterator_has_more(iterator)) {
		const char *string = oscap_string_iterator_next(iterator);
		xmlNewTextChild(parent, NULL, BAD_CAST tag_name, BAD_CAST string);
	}
	oscap_string_iterator_free(iterator);
}

void cvrf_element_add_attribute(const char *attr_name, const char *attr_value, xmlNode *element) {
	if (attr_value == NULL)
		return;

	xmlNewProp(element, BAD_CAST attr_name, BAD_CAST attr_value);
}

static void cvrf_element_add_ordinal(int ordinal, xmlNode *element) {
	char *ordinal_str = oscap_sprintf("%d", ordinal);
	xmlNewProp(element, ATTR_ORDINAL, BAD_CAST ordinal_str);
	free(ordinal_str);
}

void cvrf_element_add_child(const char *elm_name, const char *elm_value, xmlNode *parent) {
	if (elm_value == NULL)
		return;

	xmlNode *child = cvrf_element_to_dom(elm_name, elm_value);
	xmlAddChild(parent, child);
}

xmlNode *cvrf_element_to_dom(const char *elm_name, const char *elm_value) {
	if (elm_value == NULL)
		return NULL;

	xmlNode *elm_node = xmlNewNode(NULL, BAD_CAST elm_name);
	xmlNodeAddContent(elm_node, BAD_CAST elm_value);
	return elm_node;
}


/*---------------------------------------------------------------------------------------------*\
|							CVRF Parsing and Serialization Functions							|
\*---------------------------------------------------------------------------------------------*/

struct cvrf_remediation *cvrf_remediation_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_remediation *remed = cvrf_remediation_new();
	remed->type = cvrf_remediation_type_parse(reader);
	remed->date = (char *)xmlTextReaderGetAttribute(reader, TAG_DATE);
	xmlTextReaderNextElementWE(reader, TAG_REMEDIATION);
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_REMEDIATION) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}
		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DESCRIPTION) == 0) {
			remed->description = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_URL) == 0) {
			remed->url = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_ID) == 0) {
			oscap_stringlist_add_string(remed->product_ids, oscap_element_string_get(reader));
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_GROUP_ID) == 0) {
			oscap_stringlist_add_string(remed->group_ids, oscap_element_string_get(reader));
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), BAD_CAST "Entitlement") == 0) {
			remed->entitlement = oscap_element_string_copy(reader);
		}
		xmlTextReaderNextNode(reader);
	}
	xmlTextReaderNextNode(reader);
	return remed;
}

xmlNode *cvrf_remediation_to_dom(const struct cvrf_remediation *remed) {
	xmlNode *remed_node = xmlNewNode(NULL, TAG_REMEDIATION);
	cvrf_element_add_attribute("Type", cvrf_remediation_type_get_text(remed->type), remed_node);

	xmlNode *desc_node = cvrf_element_to_dom("Description", remed->description);
	xmlNewProp(desc_node, ATTR_LANG, BAD_CAST "en");
	xmlAddChild(remed_node, desc_node);
	cvrf_element_add_child("URL", remed->url, remed_node);
	cvrf_element_add_child("Entitlement", remed->entitlement, remed_node);
	cvrf_element_add_stringlist(remed->product_ids, "ProductID", remed_node);
	cvrf_element_add_stringlist(remed->group_ids, "GroupID", remed_node);
	return remed_node;
}

struct cvrf_score_set *cvrf_score_set_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_score_set *score_set = cvrf_score_set_new();
	xmlTextReaderNextElementWE(reader, TAG_SCORE_SET);
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_SCORE_SET) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}

		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_VECTOR) == 0) {
			score_set->vector = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_ID) == 0) {
			oscap_stringlist_add_string(score_set->product_ids, oscap_element_string_copy(reader));
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_BASE_SCORE) == 0) {
			cvrf_score_set_add_metric(score_set, CVSS_BASE, oscap_element_string_copy(reader));
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_ENVIRONMENTAL_SCORE) == 0) {
			cvrf_score_set_add_metric(score_set, CVSS_ENVIRONMENTAL, oscap_element_string_copy(reader));
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_TEMPORAL_SCORE) == 0) {
			cvrf_score_set_add_metric(score_set, CVSS_TEMPORAL, oscap_element_string_copy(reader));
		}
		xmlTextReaderNextNode(reader);
	}
	xmlTextReaderNextNode(reader);
	return score_set;
}

xmlNode *cvrf_score_set_to_dom(const struct cvrf_score_set *score_set) {
	xmlNode *score_node = xmlNewNode(NULL, TAG_SCORE_SET);
	const char *base = cvrf_score_set_get_base_score(score_set);
	cvrf_element_add_child("BaseScore", base, score_node);
	const char *environmental = cvrf_score_set_get_environmental_score(score_set);
	cvrf_element_add_child("EnvironmentalScore", environmental, score_node);
	const char *temporal = cvrf_score_set_get_temporal_score(score_set);
	cvrf_element_add_child("TemporalScore", temporal, score_node);
	cvrf_element_add_child("Vector", score_set->vector, score_node);
	cvrf_element_add_stringlist(score_set->product_ids, "ProductID", score_node);

	return score_node;
}

struct cvrf_threat *cvrf_threat_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_threat *threat = cvrf_threat_new();
	threat->type = cvrf_threat_type_parse(reader);
	threat->date = (char *)xmlTextReaderGetAttribute(reader, TAG_DATE);
	xmlTextReaderNextElementWE(reader, TAG_THREAT);
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_THREAT) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}

		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DESCRIPTION) == 0) {
			threat->description = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_ID) == 0) {
			oscap_stringlist_add_string(threat->product_ids, oscap_element_string_get(reader));
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_GROUP_ID) == 0) {
			oscap_stringlist_add_string(threat->group_ids, oscap_element_string_get(reader));
		}
		xmlTextReaderNextNode(reader);
	}
	xmlTextReaderNextNode(reader);
	return threat;
}

xmlNode *cvrf_threat_to_dom(const struct cvrf_threat *threat) {
	xmlNode *threat_node = xmlNewNode(NULL, TAG_THREAT);
	cvrf_element_add_attribute("Type", cvrf_threat_type_get_text(threat->type), threat_node);
	cvrf_element_add_attribute("Date", threat->date, threat_node);

	cvrf_element_add_child("Description", threat->description, threat_node);
	cvrf_element_add_stringlist(threat->product_ids, "ProductID", threat_node);
	cvrf_element_add_stringlist(threat->group_ids, "GroupID", threat_node);
	return threat_node;
}

struct cvrf_product_status *cvrf_product_status_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_product_status *stat = cvrf_product_status_new();
	stat->type = cvrf_product_status_type_parse(reader);
	xmlTextReaderNextElementWE(reader, TAG_STATUS);
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_STATUS) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}
		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_ID) == 0) {
			const char *product_id = oscap_element_string_get(reader);
			if (product_id)
				oscap_stringlist_add_string(stat->product_ids, product_id);
		}
		xmlTextReaderNextNode(reader);
	}
	return stat;
}

xmlNode *cvrf_product_status_to_dom(const struct cvrf_product_status *stat) {
	xmlNode *status_node = xmlNewNode(NULL, TAG_STATUS);
	cvrf_element_add_attribute("Type", cvrf_product_status_type_get_text(stat->type), status_node);
	cvrf_element_add_stringlist(stat->product_ids, "ProductID", status_node);
	return status_node;
}

struct cvrf_involvement *cvrf_involvement_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_involvement *involve = cvrf_involvement_new();
	involve->status = cvrf_involvement_status_type_parse(reader);
	involve->party = cvrf_involvement_party_parse(reader);
	xmlTextReaderNextNode(reader);
	if (oscap_element_depth(reader) == 4) {
		xmlTextReaderNextNode(reader);
		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DESCRIPTION) == 0) {
			involve->description = oscap_element_string_copy(reader);
			xmlTextReaderNextNode(reader);
		}
	}
	return involve;
}

xmlNode *cvrf_involvement_to_dom(const struct cvrf_involvement *involve) {
	xmlNode *involve_node = xmlNewNode(NULL, TAG_INVOLVEMENT);
	cvrf_element_add_attribute("Status", cvrf_involvement_status_type_get_text(involve->status), involve_node);
	cvrf_element_add_attribute("Party",cvrf_doc_publisher_type_get_text(involve->party), involve_node);
	cvrf_element_add_child("Description", involve->description, involve_node);
	return involve_node;
}

struct cvrf_vulnerability_cwe *cvrf_vulnerability_cwe_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);

	struct cvrf_vulnerability_cwe *vuln_cwe = cvrf_vulnerability_cwe_new();
	vuln_cwe->id = (char *)xmlTextReaderGetAttribute(reader, TAG_ID);
	vuln_cwe->cwe = oscap_element_string_copy(reader);
	xmlTextReaderNextNode(reader);
	return vuln_cwe;
}


xmlNode *cvrf_vulnerability_cwe_to_dom(const struct cvrf_vulnerability_cwe *vuln_cwe) {
	xmlNode *cwe_node = cvrf_element_to_dom("CWE", vuln_cwe->cwe);
	cvrf_element_add_attribute("ID", vuln_cwe->id, cwe_node);
	return cwe_node;
}

struct cvrf_vulnerability *cvrf_vulnerability_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);

	struct cvrf_vulnerability *vuln = cvrf_vulnerability_new();
	vuln->ordinal = cvrf_parse_ordinal(reader);
	xmlTextReaderNextElementWE(reader, TAG_VULNERABILITY);
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_VULNERABILITY) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}

		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_TITLE) == 0) {
			vuln->title = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_ID) == 0) {
			vuln->system_name = (char *)xmlTextReaderGetAttribute(reader, BAD_CAST "SystemName");
			vuln->system_id = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DISCOVERY_DATE) == 0) {
			vuln->discovery_date = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_RELEASE_DATE) == 0) {
			vuln->release_date = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_VULNERABILITY_CVE) == 0) {
			vuln->cve_id = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), BAD_CAST "Notes") == 0) {
			cvrf_parse_container(reader, vuln->notes);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_VULNERABILITY_CWE) == 0) {
			cvrf_parse_container(reader, vuln->cwes);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_INVOLVEMENTS) == 0) {
			cvrf_parse_container(reader, vuln->involvements);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_STATUSES) == 0) {
			cvrf_parse_container(reader, vuln->product_statuses);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_STATUS) == 0) {
			struct cvrf_product_status *stat = cvrf_product_status_parse(reader);
			if (stat != NULL)
				cvrf_vulnerability_add_cvrf_product_status(vuln, stat);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_THREATS) == 0) {
			cvrf_parse_container(reader, vuln->threats);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_CVSS_SCORE_SETS) == 0) {
			cvrf_parse_container(reader, vuln->score_sets);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_REMEDIATIONS) == 0) {
			cvrf_parse_container(reader, vuln->remediations);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_REFERENCES) == 0) {
			cvrf_parse_container(reader, vuln->references);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_ACKNOWLEDGMENTS) == 0) {
			cvrf_parse_container(reader, vuln->acknowledgments);
		}
		xmlTextReaderNextNode(reader);
	}
	xmlTextReaderNextNode(reader);
	return vuln;
}

xmlNode *cvrf_vulnerability_to_dom(const struct cvrf_vulnerability *vuln) {
	xmlNode *vuln_node = xmlNewNode(NULL, TAG_VULNERABILITY);
	cvrf_element_add_ordinal(vuln->ordinal, vuln_node);
	xmlNewNs(vuln_node, VULN_NS, NULL);

	cvrf_element_add_child("Title", vuln->title, vuln_node);
	if (vuln->system_id) {
		xmlNode *id_node = xmlNewTextChild(vuln_node, NULL, BAD_CAST "ID", BAD_CAST vuln->system_id);
		cvrf_element_add_attribute("SystemName", vuln->system_name, id_node);
	}
	cvrf_element_add_container(vuln->notes, CVRF_NOTE, vuln_node);
	cvrf_element_add_child("DiscoveryDate", vuln->discovery_date, vuln_node);
	cvrf_element_add_child("ReleaseDate", vuln->release_date, vuln_node);
	cvrf_element_add_container(vuln->involvements, CVRF_INVOLVEMENT, vuln_node);
	cvrf_element_add_child("CVE", vuln->cve_id, vuln_node);
	cvrf_list_to_dom(vuln->cwes, vuln_node, CVRF_VULNERABILITY_CWE);

	cvrf_element_add_container(vuln->product_statuses, CVRF_PRODUCT_STATUS, vuln_node);
	cvrf_element_add_container(vuln->threats, CVRF_THREAT, vuln_node);
	cvrf_element_add_container(vuln->score_sets, CVRF_SCORE_SET, vuln_node);
	cvrf_element_add_container(vuln->remediations, CVRF_REMEDIATION, vuln_node);
	cvrf_element_add_container(vuln->references, CVRF_REFERENCE, vuln_node);
	cvrf_element_add_container(vuln->acknowledgments, CVRF_ACKNOWLEDGMENT, vuln_node);

	return vuln_node;
}

struct cvrf_product_name *cvrf_product_name_parse(xmlTextReaderPtr reader) {
	struct cvrf_product_name *full_name = cvrf_product_name_new();
	full_name->product_id = (char *)xmlTextReaderGetAttribute(reader, TAG_PRODUCT_ID);
	full_name->cpe = oscap_element_string_copy(reader);
	xmlTextReaderNextNode(reader);
	return full_name;
}

xmlNode *cvrf_product_name_to_dom(struct cvrf_product_name *full_name) {
	if (full_name->cpe == NULL)
		return NULL;

	xmlNode *name_node = cvrf_element_to_dom("FullProductName", full_name->cpe);
	cvrf_element_add_attribute("ProductID", full_name->product_id, name_node);
	return name_node;
}

struct cvrf_group *cvrf_group_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_group *group = cvrf_group_new();
	group->group_id = (char *)xmlTextReaderGetAttribute(reader, TAG_GROUP_ID);
	xmlTextReaderNextElementWE(reader, TAG_GROUP);
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_GROUP) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}
		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DESCRIPTION) == 0) {
			group->description = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_ID) == 0) {
			oscap_stringlist_add_string(group->product_ids, oscap_element_string_get(reader));
		}
		xmlTextReaderNextNode(reader);
	}
	xmlTextReaderNextNode(reader);
	return group;
}

xmlNode *cvrf_group_to_dom(const struct cvrf_group *group) {
	xmlNode *group_node = xmlNewNode(NULL, TAG_GROUP);
	cvrf_element_add_attribute("GroupID", group->group_id, group_node);
	cvrf_element_add_child("Description", group->description, group_node);
	cvrf_element_add_stringlist(group->product_ids, "ProductID", group_node);
	return group_node;
}

struct cvrf_relationship *cvrf_relationship_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_relationship *relation = cvrf_relationship_new();
	relation->product_reference = (char *)xmlTextReaderGetAttribute(reader, ATTR_PRODUCT_REFERENCE);
	relation->relation_type = cvrf_relationship_type_parse(reader);
	relation->relates_to_ref = (char *)xmlTextReaderGetAttribute(reader, ATTR_RELATES_TO_REF);
	xmlTextReaderNextElementWE(reader, TAG_RELATIONSHIP);
	if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_NAME) == 0) {
		relation->product_name = cvrf_product_name_parse(reader);
	}
	xmlTextReaderNextNode(reader);
	return relation;
}

xmlNode *cvrf_relationship_to_dom(const struct cvrf_relationship *relation) {
	xmlNode *relation_node = xmlNewNode(NULL, TAG_RELATIONSHIP);
	cvrf_element_add_attribute("ProductReference", relation->product_reference, relation_node);
	cvrf_element_add_attribute("RelationType", cvrf_relationship_type_get_text(relation->relation_type), relation_node);
	cvrf_element_add_attribute("RelatesToProductReference", relation->relates_to_ref, relation_node);
	xmlAddChild(relation_node, cvrf_product_name_to_dom(relation->product_name));
	return relation_node;
}

struct cvrf_branch *cvrf_branch_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_branch *branch = cvrf_branch_new();
	branch->branch_name = (char *)xmlTextReaderGetAttribute(reader, TAG_NAME);
	branch->type = cvrf_branch_type_parse(reader);
	xmlTextReaderNextElement(reader);
	if (!xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_NAME)) {
		branch->product_name = cvrf_product_name_parse(reader);
		xmlTextReaderNextNode(reader);
		xmlTextReaderNextNode(reader);
	} else {
		while(xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_BRANCH) == 0) {
			if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
				xmlTextReaderNextNode(reader);
				continue;
			}
			if (!oscap_list_add(branch->subbranches, cvrf_branch_parse(reader))) {
				cvrf_set_parsing_error("Branch");
			}
			xmlTextReaderNextNode(reader);
		}
	}
	xmlTextReaderNextNode(reader);
	return branch;
}

xmlNode *cvrf_branch_to_dom(struct cvrf_branch *branch) {
	xmlNode *branch_node = xmlNewNode(NULL, TAG_BRANCH);
	cvrf_element_add_attribute("Type", cvrf_branch_type_get_text(branch->type), branch_node);
	cvrf_element_add_attribute("Name", branch->branch_name, branch_node);

	if (branch->type == CVRF_BRANCH_PRODUCT_FAMILY) {
		cvrf_list_to_dom(branch->subbranches, branch_node, CVRF_BRANCH);
	} else {
		xmlAddChild(branch_node, cvrf_product_name_to_dom(branch->product_name));
	}
	return branch_node;
}

struct cvrf_product_tree *cvrf_product_tree_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_product_tree *tree = cvrf_product_tree_new();
	if (xmlTextReaderIsEmptyElement(reader) == 1) {
		cvrf_set_parsing_error("ProductTree");
		cvrf_product_tree_free(tree);
		return NULL;
	}
	xmlTextReaderNextElementWE(reader, TAG_PRODUCT_TREE);
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_TREE) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}
		if (!xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_NAME)) {
			if (!oscap_list_add(tree->product_names, cvrf_product_name_parse(reader)))
				cvrf_set_parsing_error("FullProductName");
		} else if (!xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_BRANCH)) {
			while(xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_BRANCH) == 0) {
				if (!oscap_list_add(tree->branches, cvrf_branch_parse(reader)))
					cvrf_set_parsing_error("Branch");
			}
		} else if (!xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_RELATIONSHIP)) {
			if (!oscap_list_add(tree->relationships, cvrf_relationship_parse(reader)))
				cvrf_set_parsing_error("Relationship");
		} else if (!xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_GROUPS)) {
			cvrf_parse_container(reader, tree->product_groups);
		}
		xmlTextReaderNextNode(reader);
	}
	xmlTextReaderNextElement(reader);
	return tree;
}

xmlNode *cvrf_product_tree_to_dom(struct cvrf_product_tree *tree) {
	xmlNode *tree_node = xmlNewNode(NULL, TAG_PRODUCT_TREE);
	xmlNewNs(tree_node, PROD_NS, NULL);
	cvrf_list_to_dom(tree->product_names, tree_node, CVRF_PRODUCT_NAME);
	cvrf_list_to_dom(tree->branches, tree_node, CVRF_BRANCH);
	cvrf_list_to_dom(tree->relationships, tree_node, CVRF_RELATIONSHIP);
	cvrf_element_add_container(tree->product_groups, CVRF_GROUP, tree_node);
	return tree_node;
}

struct cvrf_acknowledgment *cvrf_acknowledgment_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_acknowledgment *ack = cvrf_acknowledgment_new();
	xmlTextReaderNextElementWE(reader, TAG_ACKNOWLEDGMENT);
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_ACKNOWLEDGMENT) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}
		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_NAME) == 0) {
			oscap_stringlist_add_string(ack->names, oscap_element_string_get(reader));
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_ORGANIZATION) == 0) {
			oscap_stringlist_add_string(ack->organizations, oscap_element_string_get(reader));
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_URL) == 0) {
			oscap_stringlist_add_string(ack->urls, oscap_element_string_get(reader));
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DESCRIPTION) == 0) {
			ack->description = oscap_element_string_copy(reader);
		}
		xmlTextReaderNextNode(reader);
	}
	xmlTextReaderNextNode(reader);
	return ack;
}

xmlNode *cvrf_acknowledgment_to_dom(struct cvrf_acknowledgment *ack) {
	xmlNode *ack_node = xmlNewNode(NULL, TAG_ACKNOWLEDGMENT);
	cvrf_element_add_stringlist(ack->names, "Name", ack_node);
	cvrf_element_add_stringlist(ack->organizations, "Organization", ack_node);
	cvrf_element_add_child("Description", ack->description, ack_node);
	cvrf_element_add_stringlist(ack->urls, "URL", ack_node);
	return ack_node;
}

struct cvrf_reference *cvrf_reference_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_reference *ref = cvrf_reference_new();
	ref->type = cvrf_reference_type_parse(reader);
	xmlTextReaderNextElementWE(reader, TAG_REFERENCE);
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_REFERENCE) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}
		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_URL) == 0) {
			ref->url = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DESCRIPTION) == 0) {
			ref->description = oscap_element_string_copy(reader);
		}
		xmlTextReaderNextNode(reader);
	}
	xmlTextReaderNextNode(reader);
	return ref;
}

xmlNode *cvrf_reference_to_dom(struct cvrf_reference *ref) {
	xmlNode *ref_node = xmlNewNode(NULL, TAG_REFERENCE);
	cvrf_element_add_attribute("Type", cvrf_reference_type_get_text(ref->type), ref_node);
	cvrf_element_add_child("URL", ref->url, ref_node);
	cvrf_element_add_child("Description", ref->description, ref_node);
	return ref_node;
}

struct cvrf_note *cvrf_note_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_note *note = cvrf_note_new();
	if (xmlTextReaderIsEmptyElement(reader) == 1) {
		cvrf_set_parsing_error("Note");
		cvrf_note_free(note);
		return NULL;
	}

	note->ordinal = cvrf_parse_ordinal(reader);
	note->type = cvrf_note_type_parse(reader);
	note->audience = (char *)xmlTextReaderGetAttribute(reader, BAD_CAST "Audience");
	note->title = (char *)xmlTextReaderGetAttribute(reader, TAG_TITLE);
	xmlTextReaderNextNode(reader);
	note->contents =oscap_element_string_copy(reader);
	xmlTextReaderNextNode(reader);
	xmlTextReaderNextNode(reader);
	return note;
}

xmlNode *cvrf_note_to_dom(struct cvrf_note *note) {
	xmlNode *note_node = cvrf_element_to_dom("Note", note->contents);
	cvrf_element_add_ordinal(note->ordinal, note_node);
	cvrf_element_add_attribute("Type", cvrf_note_type_get_text(note->type), note_node);
	cvrf_element_add_attribute("Title", note->title, note_node);
	cvrf_element_add_attribute("Audience", note->audience, note_node);
	return note_node;
}

struct cvrf_revision *cvrf_revision_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_revision *revision = cvrf_revision_new();
	xmlTextReaderNextElementWE(reader, TAG_REVISION);
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_REVISION) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}
		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_NUMBER) == 0) {
			revision->number = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DATE) == 0) {
			revision->date = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DESCRIPTION) == 0) {
			revision->description = oscap_element_string_copy(reader);
		}
		xmlTextReaderNextNode(reader);
	}
	xmlTextReaderNextNode(reader);
	return revision;
}

xmlNode *cvrf_revision_to_dom(struct cvrf_revision *revision) {
	xmlNode *revision_node = xmlNewNode(NULL, TAG_REVISION);
	cvrf_element_add_child("Number", revision->number, revision_node);
	cvrf_element_add_child("Date", revision->date, revision_node);
	cvrf_element_add_child("Description", revision->description, revision_node);
	return revision_node;
}

struct cvrf_doc_tracking *cvrf_doc_tracking_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_doc_tracking *tracking = cvrf_doc_tracking_new();
	if (xmlTextReaderIsEmptyElement(reader) == 1) {
		cvrf_set_parsing_error("DocumentTracking");
		cvrf_doc_tracking_free(tracking);
		return NULL;
	}

	xmlTextReaderNextElement(reader);
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DOCUMENT_TRACKING) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}
		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_IDENTIFICATION) == 0) {
			xmlTextReaderNextElementWE(reader, TAG_IDENTIFICATION);
			tracking->tracking_id = cvrf_parse_element(reader, "ID", false);
			while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_IDENTIFICATION) != 0) {
				if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_ALIAS) == 0) {
					oscap_stringlist_add_string(tracking->aliases, cvrf_parse_element(reader, "Alias", false));
					xmlTextReaderNextNode(reader);
				}
				xmlTextReaderNextNode(reader);
			}
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_STATUS) == 0) {
			tracking->status = cvrf_doc_status_type_parse(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_VERSION) == 0) {
			tracking->version = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_REVISION_HISTORY) == 0) {
			cvrf_parse_container(reader, tracking->revision_history);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), BAD_CAST "InitialReleaseDate") == 0) {
			tracking->init_release_date = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), BAD_CAST "CurrentReleaseDate") == 0) {
			tracking->cur_release_date = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_GENERATOR) == 0) {
			xmlTextReaderNextElementWE(reader, TAG_GENERATOR);
			if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_GENERATOR_ENGINE) == 0) {
				tracking->generator_engine = oscap_element_string_copy(reader);
				xmlTextReaderNextElementWE(reader, TAG_GENERATOR);
			}
			if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DATE) == 0) {
				tracking->generator_date = oscap_element_string_copy(reader);
			}
		}
		xmlTextReaderNextNode(reader);
	}
	return tracking;
}

xmlNode *cvrf_doc_tracking_to_dom(struct cvrf_doc_tracking *tracking) {
	xmlNode *tracking_node = xmlNewNode(NULL, TAG_DOCUMENT_TRACKING);
	if (tracking->tracking_id) {
		xmlNode *ident_node = xmlNewTextChild(tracking_node, NULL, TAG_IDENTIFICATION, NULL);
		cvrf_element_add_child("ID", tracking->tracking_id, ident_node);
		cvrf_element_add_stringlist(tracking->aliases, "Alias", ident_node);
	}
	const char *status = cvrf_doc_status_type_get_text(tracking->status);
	cvrf_element_add_child("Status", status, tracking_node);
	cvrf_element_add_child("Version", tracking->version, tracking_node);
	cvrf_element_add_container(tracking->revision_history, CVRF_REVISION, tracking_node);
	cvrf_element_add_child("InitialReleaseDate", tracking->init_release_date, tracking_node);
	cvrf_element_add_child("CurrentReleaseDate", tracking->cur_release_date, tracking_node);
	if (tracking->generator_engine) {
		xmlNode *generator_node = xmlNewTextChild(tracking_node, NULL, BAD_CAST "Generator", NULL);
		cvrf_element_add_child("Engine", tracking->generator_engine, generator_node);
		cvrf_element_add_child("Date", tracking->generator_date, generator_node);
	}
	return tracking_node;
}

struct cvrf_doc_publisher *cvrf_doc_publisher_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	struct cvrf_doc_publisher *publisher = cvrf_doc_publisher_new();
	publisher->type = cvrf_doc_publisher_type_parse(reader);
	if (publisher->type == CVRF_DOC_PUBLISHER_UNKNOWN && xmlTextReaderIsEmptyElement(reader) == 1) {
		cvrf_set_parsing_error("DocumentPublisher");
		cvrf_doc_publisher_free(publisher);
		return NULL;
	}
	publisher->vendor_id = (char *)xmlTextReaderGetAttribute(reader, ATTR_VENDOR_ID);
	xmlTextReaderNextElementWE(reader, TAG_PUBLISHER);
	publisher->contact_details = cvrf_parse_element(reader, "ContactDetails", true);
	publisher->issuing_authority = cvrf_parse_element(reader, "IssuingAuthority", false);
	xmlTextReaderNextNode(reader);
	return publisher;
}

xmlNode *cvrf_doc_publisher_to_dom(struct cvrf_doc_publisher *publisher) {
	xmlNode *pub_node = xmlNewNode(NULL, TAG_PUBLISHER);
	cvrf_element_add_attribute("Type", cvrf_doc_publisher_type_get_text(publisher->type), pub_node);
	cvrf_element_add_child("ContactDetails", publisher->contact_details, pub_node);
	cvrf_element_add_child("IssuingAuthority", publisher->issuing_authority, pub_node);
	return pub_node;
}

struct cvrf_document *cvrf_document_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);

	struct cvrf_document *doc = cvrf_document_new();
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_TREE) != 0) {
		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}
		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PUBLISHER) == 0) {
			doc->publisher = cvrf_doc_publisher_parse(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DOCUMENT_TRACKING) == 0) {
			doc->tracking = cvrf_doc_tracking_parse(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), BAD_CAST "DocumentNotes") == 0) {
			cvrf_parse_container(reader, doc->doc_notes);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DISTRIBUTION) == 0) {
			doc->doc_distribution = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_AGGREGATE_SEVERITY) == 0) {
			doc->namespace = (char *)xmlTextReaderGetAttribute(reader, ATTR_NAMESPACE);
			doc->aggregate_severity = oscap_element_string_copy(reader);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_DOCUMENT_REFERENCES) == 0) {
			cvrf_parse_container(reader, doc->doc_references);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_ACKNOWLEDGMENTS) == 0) {
			cvrf_parse_container(reader, doc->acknowledgments);
		}
		xmlTextReaderNextNode(reader);
	}
	return doc;
}

xmlNode *cvrf_document_to_dom(struct cvrf_document *document) {
	xmlNode *pub_node = cvrf_doc_publisher_to_dom(document->publisher);
	xmlAddNextSibling(pub_node, cvrf_doc_tracking_to_dom(document->tracking));
	xmlAddSibling(pub_node, cvrf_list_to_dom(document->doc_notes, NULL, CVRF_DOCUMENT_NOTE));

	xmlNode *distribution = cvrf_element_to_dom("DocumentDistribution", document->doc_distribution);
	cvrf_element_add_attribute("xml:lang", "en", distribution);
	xmlAddSibling(pub_node, distribution);

	xmlNode *severity = cvrf_element_to_dom("AggregateSeverity", document->aggregate_severity);
	cvrf_element_add_attribute("Namespace", document->namespace, severity);
	xmlAddSibling(distribution, severity);

	xmlAddSibling(pub_node, cvrf_list_to_dom(document->doc_references, NULL, CVRF_DOCUMENT_REFERENCE));
	xmlAddSibling(pub_node, cvrf_list_to_dom(document->acknowledgments, NULL, CVRF_ACKNOWLEDGMENT));
	return pub_node;
}

struct cvrf_model *cvrf_model_parse(xmlTextReaderPtr reader) {
	__attribute__nonnull__(reader);
	if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_CVRF_DOC) != 0 ||
			xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT)
		return NULL;

	struct cvrf_model *ret = cvrf_model_new();
	xmlTextReaderNextElement(reader);
	ret->doc_title = cvrf_parse_element(reader, "DocumentTitle", true);
	ret->doc_type = cvrf_parse_element(reader, "DocumentType", true);
	ret->document = cvrf_document_parse(reader);
	if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_TREE) == 0) {
		ret->tree = cvrf_product_tree_parse(reader);
	}
	cvrf_parse_container(reader, ret->vulnerabilities);
	return ret;
}

xmlNode *cvrf_model_to_dom(struct cvrf_model *model, xmlDocPtr doc, xmlNode *parent, void *user_args) {
	xmlNode *root_node = xmlNewNode(NULL, BAD_CAST "cvrfdoc");
	if (parent == NULL) {
		xmlDocSetRootElement(doc, root_node);
	} else {
		xmlAddChild(parent, root_node);
	}
	xmlNewNs(root_node, CVRF_NS, NULL);
	xmlNewNs(root_node, CVRF_NS, BAD_CAST "cvrf");
	xmlNode *title_node = xmlNewTextChild(root_node, NULL, TAG_DOC_TITLE, BAD_CAST model->doc_title);
	cvrf_element_add_attribute("xml:lang", "en", title_node);
	cvrf_element_add_child("DocumentType", model->doc_type, root_node);
	xmlAddChildList(root_node, cvrf_document_to_dom(model->document));

	xmlAddChild(root_node, cvrf_product_tree_to_dom(model->tree));
	cvrf_list_to_dom(model->vulnerabilities, root_node, CVRF_VULNERABILITY);
	return root_node;
}

struct cvrf_index *cvrf_index_parse_xml(struct oscap_source *index_source) {
	__attribute__nonnull__(index_source);

	char *buffer = "";
	char **buffer_ptr = &buffer;
	/* oscap_source_get_raw_memory doesn't check for null pointers s.a. null size */
	size_t size = 0;
	if (oscap_source_get_raw_memory(index_source, buffer_ptr, &size) == 1) {
		return NULL;
	}
	struct cvrf_index *index = cvrf_index_new();
	cvrf_index_set_index_file(index, oscap_source_readable_origin(index_source));
	oscap_source_free(index_source);
	return index;
}

xmlNode *cvrf_index_to_dom(struct cvrf_index *index, xmlDocPtr doc, xmlNode *parent, void *user_args) {
	xmlNode *index_node = xmlNewNode(NULL, BAD_CAST "Index");
	if (parent == NULL) {
		xmlDocSetRootElement(doc, index_node);
	} else {
		xmlAddChild(parent, index_node);
	}

	struct cvrf_model_iterator *models = cvrf_index_get_models(index);
	while (cvrf_model_iterator_has_more(models)) {
		struct cvrf_model *model = cvrf_model_iterator_next(models);
		cvrf_model_to_dom(model, doc, index_node, user_args);
	}
	cvrf_model_iterator_free(models);

	return index_node;
}

/* End of export functions
 * */
/***************************************************************************/

