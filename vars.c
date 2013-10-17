/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2009 Sun Microsystems, Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * Portions Copyright 2008 Denis Cheng
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "filebench.h"
#include "vars.h"
#include "misc.h"
#include "utils.h"
#include "stats.h"
#include "eventgen.h"
#include "fb_random.h"

static var_t *var_find_special(char *name);

/*
 * The filebench variables system has attribute value descriptors (avd_t) where
 * an avd contains a boolean, integer, double, string, random distribution
 * object ptr, boolean ptr, integer ptr, double ptr, string ptr, or variable
 * ptr. The system also has the variables themselves, (var_t), which are named
 * and typed entities that can be allocated, selected and changed using the
 * "set" command and used in attribute assignments. The variables contain
 * either a boolean, an integer, a double, a string or pointer to an associated
 * random distribution object. Both avd_t and var_t entities are allocated from
 * interprocess shared memory space.
 * 
 * The attribute descriptors implement delayed binding to variable values,
 * which is necessary because the values of variables may be changed between
 * the time the workload model is loaded and the time it actually runs by
 * further "set" commands.
 *
 * For static attributes, the value is just loaded in the descriptor directly,
 * avoiding the need to allocate a variable to hold the static value.
 *
 * For random variables, they actually point to the random distribution object,
 * allowing Filebench to invoke the appropriate random distribution function on
 * each access to the attribute. 
 *
 * The routines in this module are used to allocate, locate, and manipulate the
 * attribute descriptors and vars. Routines are also included to convert
 * between the component strings, doubles and integers of vars, and said
 * components of avd_t.
 */

static char *
avd_get_type_string(avd_t avd)
{
	switch (avd->avd_type) {
	case AVD_INVALID:
		return "uninitialized";
	case AVD_VAL_BOOL:
		return "boolean value";
	case AVD_VARVAL_BOOL:
		return "points to boolean in var_t";
	case AVD_VAL_INT:
		return "integer value";
	case AVD_VARVAL_INT:
		return "points to integer in var_t";
	case AVD_VAL_STR:
		return "string";
	case AVD_VARVAL_STR:
		return "points to string in var_t";
	case AVD_VAL_DBL:
		return "double float value";
	case AVD_VARVAL_DBL:
		return "points to double float in var_t";
	case AVD_RANDVAR:
		return "points to var_t's random distribution object";
	default:
		return "illegal avd type";
	}
}

uint64_t
avd_get_int(avd_t avd)
{
	randdist_t *rndp;

	if (!avd)
		return 0;

	switch (avd->avd_type) {
	case AVD_VAL_INT:
		return avd->avd_val.intval;

	case AVD_VARVAL_INT:
		if (avd->avd_val.intptr)
			return *(avd->avd_val.intptr);
		return 0;

	case AVD_RANDVAR:
		rndp = avd->avd_val.randptr;
		if (rndp)
			return (uint64_t)rndp->rnd_get(rndp);
		return 0;

	default:
		filebench_log(LOG_ERROR,
			"Attempt to get integer from %s avd",
			avd_get_type_string(avd));
		return 0;
	}
}

double
avd_get_dbl(avd_t avd)
{
	randdist_t *rndp;

	if (!avd)
		return 0.0;

	switch (avd->avd_type) {
	case AVD_VAL_INT:
		return (double)avd->avd_val.intval;

	case AVD_VAL_DBL:
		return avd->avd_val.dblval;

	case AVD_VARVAL_INT:
		if (avd->avd_val.intptr)
			return (double)(*(avd->avd_val.intptr));
		return 0.0;

	case AVD_VARVAL_DBL:
		if (avd->avd_val.dblptr)
			return *(avd->avd_val.dblptr);
		return 0.0;

	case AVD_RANDVAR:
		rndp = avd->avd_val.randptr;
		if (rndp)
			return rndp->rnd_get(rndp);
		return 0.0;

	default:
		filebench_log(LOG_ERROR,
			"Attempt to get floating point from %s avd",
			avd_get_type_string(avd));
		return 0.0;
	}
}

boolean_t
avd_get_bool(avd_t avd)
{
	if (!avd)
		return 0;

	switch (avd->avd_type) {
	case AVD_VAL_BOOL:
		return avd->avd_val.boolval;

	case AVD_VARVAL_BOOL:
		if (avd->avd_val.boolptr)
			return *(avd->avd_val.boolptr);
		return FALSE;

	case AVD_VAL_INT:
		if (avd->avd_val.intval)
			return TRUE;
		return FALSE;

	case AVD_VARVAL_INT:
		if (avd->avd_val.intptr && *(avd->avd_val.intptr))
			return TRUE;
		return FALSE;

	default:
		filebench_log(LOG_ERROR,
			"Attempt to get boolean from %s avd",
			avd_get_type_string(avd));
		return FALSE;
	}
}

char *
avd_get_str(avd_t avd)
{
	if (!avd)
		return NULL;

	switch (avd->avd_type) {
	case AVD_VAL_STR:
		return avd->avd_val.strval;

	case AVD_VARVAL_STR:
		if (avd->avd_val.strptr)
			return *avd->avd_val.strptr;
		return NULL;

	default:
		filebench_log(LOG_ERROR,
			"Attempt to get string from %s avd",
			avd_get_type_string(avd));
		return NULL;
	}
}

static avd_t
avd_alloc_cmn(void)
{
	avd_t avd;

	avd = (avd_t)ipc_malloc(FILEBENCH_AVD);
	if (!avd)
		filebench_log(LOG_ERROR, "AVD allocation failed");

	return avd;
}

avd_t
avd_bool_alloc(boolean_t val)
{
	avd_t avd;

	avd = avd_alloc_cmn();
	if (!avd)
		return NULL;

	avd->avd_type = AVD_VAL_BOOL;
	avd->avd_val.boolval = val;

	return avd;
}

avd_t
avd_int_alloc(uint64_t val)
{
	avd_t avd;

	avd = avd_alloc_cmn();
	if (!avd)
		return NULL;

	avd->avd_type = AVD_VAL_INT;
	avd->avd_val.intval = val;

	return avd;
}

avd_t
avd_str_alloc(char *string)
{
	avd_t avd;

	if (!string) {
		filebench_log(LOG_ERROR, "No string supplied\n");
		return NULL;
	}

	avd = avd_alloc_cmn();
	if (!avd)
		return NULL;

	avd->avd_type = AVD_VAL_STR;
	avd->avd_val.strval = ipc_stralloc(string);

	return avd;
}

/*
 * Allocates an avd_t and points it to the var that
 * it will eventually be filled from.
 */
static avd_t
avd_alloc_var_ptr(var_t *var)
{
	avd_t avd;

	if (!var)
		return NULL;

	avd = avd_alloc_cmn();
	if (!avd)
		return NULL;

	switch (var->var_type & VAR_TYPE_SET_MASK) {
	case VAR_TYPE_BOOL_SET:
		avd->avd_type = AVD_VARVAL_BOOL;
		avd->avd_val.boolptr = &var->var_val.boolean;
		break;

	case VAR_TYPE_INT_SET:
		avd->avd_type = AVD_VARVAL_INT;
		avd->avd_val.intptr = &var->var_val.integer;
		break;

	case VAR_TYPE_STR_SET:
		avd->avd_type = AVD_VARVAL_STR;
		avd->avd_val.strptr = &var->var_val.string;
		break;

	case VAR_TYPE_DBL_SET:
		avd->avd_type = AVD_VARVAL_DBL;
		avd->avd_val.dblptr = &var->var_val.dbl_flt;
		break;

	case VAR_TYPE_RAND_SET:
		avd->avd_type = AVD_RANDVAR;
		avd->avd_val.randptr = var->var_val.randptr;
		break;

	default:
		filebench_log(LOG_ERROR, "Illegal  variable type");
		return NULL;
	}

	return avd;
}

static var_t *
var_alloc_cmn(char *name, int var_type)
{
	var_t **var_listp;
	var_t *var = NULL;
	var_t *prev = NULL;
	var_t *newvar;

	newvar = (var_t *)ipc_malloc(FILEBENCH_VARIABLE);
	if (!newvar) {
		filebench_log(LOG_ERROR, "Out of memory for variables");
		return NULL;
	}

	memset(newvar, 0, sizeof(*newvar));

	newvar->var_type = var_type;

	newvar->var_name = ipc_stralloc(name);
	if (!newvar->var_name) {
		filebench_log(LOG_ERROR, "Out of memory for strings");
		return NULL;
	}

	switch (var_type & VAR_TYPE_MASK) {
	case VAR_TYPE_RANDOM:
	case VAR_TYPE_NORMAL:
		var_listp = &filebench_shm->shm_var_list;
		break;

	case VAR_TYPE_SPECIAL:
		var_listp = &filebench_shm->shm_var_special_list;
		break;

	case VAR_TYPE_LOCAL:
		/* place on head of shared local list */
		newvar->var_next = filebench_shm->shm_var_loc_list;
		filebench_shm->shm_var_loc_list = newvar;
		return newvar;

	default:
		filebench_log(LOG_ERROR, "Illegal variable type");
		return NULL;
	}

	/* add to the end of list */
	for (var = *var_listp; var; var = var->var_next)
		prev = var;
	if (prev)
		prev->var_next = newvar;
	else
		*var_listp = newvar;

	return newvar;
}

static var_t *
var_alloc(char *name)
{
	return var_alloc_cmn(name, VAR_TYPE_NORMAL);
}

static var_t *
var_alloc_special(char *name)
{
	return var_alloc_cmn(name, VAR_TYPE_SPECIAL);
}

/*
 * Searches for var_t with name "name" in the shm_var_loc_list,
 * then, if not found, in the normal shm_var_list. If a matching
 * local or normal var is found, returns a pointer to the var_t,
 * otherwise returns NULL.
 */
static var_t *
var_find(char *name)
{
	var_t *var;

	for (var = filebench_shm->shm_var_loc_list; var; var = var->var_next) {
		if (!strcmp(var->var_name, name))
			return var;
	}

	for (var = filebench_shm->shm_var_list; var; var = var->var_next) {
		if (!strcmp(var->var_name, name))
			return (var);
	}

	return NULL;
}

/*
 * Searches for var_t with name "name" in the supplied list.
 */
static var_t *
var_find_list_only(char *name, var_t *var_list)
{
	var_t *var;

	for (var = var_list; var; var = var->var_next) {
		if (!strcmp(var->var_name, name))
			return var;
	}

	return NULL;
}

/*
 * Searches for the named var and returns it if found. If not
 * found it allocates a new variable
 */
static var_t *
var_find_alloc(char *name)
{
	var_t *var;

	if (!name) {
		filebench_log(LOG_ERROR,
			"var_find_alloc: Var name not supplied");
		return NULL;
	}

	/* ommits $ sign in the beginning */
	name += 1;

	var = var_find(name);
	if (!var)
		var = var_alloc(name);

	return var;
}

int
var_assign_boolean(char *name, boolean_t bool)
{
	var_t *var;

	var = var_find_alloc(name);
	if (!var) {
		filebench_log(LOG_ERROR, "Cannot assign variable %s", name);
		return -1;
	}

	if ((var->var_type & VAR_TYPE_MASK) == VAR_TYPE_RANDOM) {
		filebench_log(LOG_ERROR,
			"Cannot assign boolean to random variable %s", name);
		return -1;
	}

	VAR_SET_BOOL(var, bool);

	return 0;
}

int
var_assign_integer(char *name, uint64_t integer)
{
	var_t *var;

	var = var_find_alloc(name);
	if (!var) {
		filebench_log(LOG_ERROR, "Cannot assign variable %s", name);
		return -1;
	}

	if ((var->var_type & VAR_TYPE_MASK) == VAR_TYPE_RANDOM) {
		filebench_log(LOG_ERROR,
			"Cannot assign integer to random variable %s", name);
		return -1;
	}

	VAR_SET_INT(var, integer);

	filebench_log(LOG_DEBUG_SCRIPT, "Assign integer %s=%llu",
				name, (u_longlong_t)integer);

	return 0;
}

/*
 * Find a variable, and set it to random type.
 * If it does not have a random extension, allocate one.
 */
var_t *
var_find_randvar(char *name)
{
	var_t *newvar;

	name += 1;

	newvar = var_find(name);
	if (!newvar) {
		filebench_log(LOG_ERROR,
			"failed to locate random variable $%s\n", name);
		return NULL;
	}

	/* set randdist pointer unless it is already set */
	if (((newvar->var_type & VAR_TYPE_MASK) != VAR_TYPE_RANDOM) ||
		!VAR_HAS_RANDDIST(newvar)) {
		filebench_log(LOG_ERROR,
			"Found variable $%s not random\n", name);
		return NULL;
	}

	return newvar;
}

/*
 * Allocate a variable, and set it to random type. Then
 * allocate a random extension.
 */
var_t *
var_define_randvar(char *name)
{
	var_t *newvar;
	randdist_t *rndp = NULL;

	name += 1;

	/* make sure variable doesn't already exist */
	if (var_find(name)) {
		filebench_log(LOG_ERROR, "variable name already in use\n");
		return NULL;
	}

	/* allocate a random variable */
	newvar = var_alloc_cmn(name, VAR_TYPE_RANDOM);
	if (!newvar) {
		filebench_log(LOG_ERROR, "failed to alloc random variable\n");
		return NULL;
	}

	/* set randdist pointer */
	rndp = randdist_alloc();
	if (!rndp) {
		filebench_log(LOG_ERROR,
			"failed to alloc random distribution object\n");
		return NULL;
	}

	rndp->rnd_var = newvar;
	VAR_SET_RAND(newvar, rndp);

	return newvar;
}

/*
 * Searches for the named var, and if found returns an avd_t
 * pointing to the var's var_integer, var_string or var_double
 * as appropriate. If not found, attempts to allocate
 * a var named "name" and returns an avd_t to it with
 * no value set. If the var cannot be found or allocated, an
 * error is logged and the run is terminated.
 */
avd_t
var_ref_attr(char *name)
{
	var_t *var;

	name += 1;

	var = var_find(name);
	if (!var)
		var = var_find_special(name);

	if (!var)
		var = var_alloc(name);

	if (!var) {
		filebench_log(LOG_ERROR, "Invalid variable $%s", name);
		filebench_shutdown(1);
	}

	/* allocate pointer to var and return */
	return avd_alloc_var_ptr(var);
}

/*
 * Converts the contents of a var to a string.
 */
static char *
__var_to_string(var_t *var)
{
	char tmp[128];

	if ((var->var_type & VAR_TYPE_MASK) == VAR_TYPE_RANDOM) {
		switch (var->var_val.randptr->rnd_type & RAND_TYPE_MASK) {
		case RAND_TYPE_UNIFORM:
			return fb_stralloc("uniform random var");
		case RAND_TYPE_GAMMA:
			return fb_stralloc("gamma random var");
		case RAND_TYPE_TABLE:
			return fb_stralloc("tabular random var");
		default:
			return fb_stralloc("unitialized random var");
		}
	}

	if (VAR_HAS_STRING(var) && var->var_val.string)
		return fb_stralloc(var->var_val.string);

	if (VAR_HAS_BOOLEAN(var)) {
		if (var->var_val.boolean)
			return fb_stralloc("true");
		else
			return fb_stralloc("false");
	}

	if (VAR_HAS_INTEGER(var)) {
		(void) snprintf(tmp, sizeof (tmp), "%llu",
			(u_longlong_t)var->var_val.integer);
		return fb_stralloc(tmp);
	}

	return fb_stralloc("No default");
}

char *
var_to_string(char *name)
{
	var_t *var;

	name += 1;

	var = var_find(name);
	if (!var)
		var = var_find_special(name);

	if (!var)
		return NULL;

	return __var_to_string(var);
}

char *
var_randvar_to_string(char *name, int param_name)
{
	var_t *var;
	uint64_t value;
	char tmp[128];

	var = var_find(name + 1);
	if (!var)
		return var_to_string(name);

	if (((var->var_type & VAR_TYPE_MASK) != VAR_TYPE_RANDOM) ||
	 	!VAR_HAS_RANDDIST(var))
		return var_to_string(name);

	switch (param_name) {
	case RAND_PARAM_TYPE:
		switch (var->var_val.randptr->rnd_type & RAND_TYPE_MASK) {
		case RAND_TYPE_UNIFORM:
			return fb_stralloc("uniform");
		case RAND_TYPE_GAMMA:
			return fb_stralloc("gamma");
		case RAND_TYPE_TABLE:
			return fb_stralloc("tabular");
		default:
			return fb_stralloc("uninitialized");
		}

	case RAND_PARAM_SRC:
		if (var->var_val.randptr->rnd_type & RAND_SRC_GENERATOR)
			return fb_stralloc("rand48");
		else
			return fb_stralloc("urandom");

	case RAND_PARAM_SEED:
		value = avd_get_int(var->var_val.randptr->rnd_seed);
		break;

	case RAND_PARAM_MIN:
		value = avd_get_int(var->var_val.randptr->rnd_min);
		break;

	case RAND_PARAM_MEAN:
		value = avd_get_int(var->var_val.randptr->rnd_mean);
		break;

	case RAND_PARAM_GAMMA:
		value = avd_get_int(var->var_val.randptr->rnd_gamma);
		break;

	case RAND_PARAM_ROUND:
		value = avd_get_int(var->var_val.randptr->rnd_round);
		break;

	default:
		return NULL;

	}

	/* just an integer value if we got here */
	(void) snprintf(tmp, sizeof (tmp), "%llu", (u_longlong_t)value);
	return (fb_stralloc(tmp));
}

/*
 * Copies the value stored in the source variable into the destination
 * variable.  Returns -1 if any problems encountered, 0 otherwise.
 */
static int
var_copy(var_t *dst_var, var_t *src_var) {
	char *strptr;

	if (VAR_HAS_BOOLEAN(src_var))
		VAR_SET_BOOL(dst_var, src_var->var_val.boolean);

	if (VAR_HAS_INTEGER(src_var))
		VAR_SET_INT(dst_var, src_var->var_val.integer);

	if (VAR_HAS_DOUBLE(src_var))
		VAR_SET_DBL(dst_var, src_var->var_val.dbl_flt);

	if (VAR_HAS_STRING(src_var)) {
		strptr = ipc_stralloc(src_var->var_val.string);
		if (!strptr) {
			filebench_log(LOG_ERROR,
				"Cannot assign string for variable %s",
				dst_var->var_name);
			return -1;
		}
		VAR_SET_STR(dst_var, strptr);
	}

	return 0;
}

int
var_assign_string(char *name, char *string)
{
	var_t *var;
	char *strptr;

	name += 1;

	var = var_find(name);
	if (!var)
		var = var_alloc(name);

	if (!var) {
		filebench_log(LOG_ERROR, "Cannot assign variable %s",
			name);
		return -1;
	}

	if ((var->var_type & VAR_TYPE_MASK) == VAR_TYPE_RANDOM) {
		filebench_log(LOG_ERROR,
			"Cannot assign string to random variable %s", name);
		return -1;
	}

	strptr = ipc_stralloc(string);
	if (!strptr) {
		filebench_log(LOG_ERROR, "Cannot assign variable %s",
			name);
		return -1;
	}

	VAR_SET_STR(var, strptr);

	return 0;
}

/*
 * Allocates a local var (var_t) from interprocess shared memory after
 * first adjusting the name to elminate the leading $.
 */
var_t *
var_lvar_alloc_local(char *name)
{
	if (name[0] == '$')
		name += 1;

	return var_alloc_cmn(name, VAR_TYPE_LOCAL);
}

/*
 * Allocates a local var and then extracts the var_string from
 * the var named "string" and copies it into the var_string
 * of the var "name", after first allocating a piece of
 * interprocess shared string memory. Returns a pointer to the
 * newly allocated local var or NULL on error.
 */
var_t *
var_lvar_assign_var(char *name, char *src_name)
{
	var_t *dst_var, *src_var;
	char *strptr;

	src_name += 1;

	src_var = var_find(src_name);
	if (!src_var) {
		filebench_log(LOG_ERROR,
			"Cannot find source variable %s", src_name);
		return NULL;
	}

	dst_var = var_lvar_alloc_local(name);
	if (!dst_var) {
		filebench_log(LOG_ERROR, "Cannot assign variable %s", name);
		return NULL;
	}

	if (VAR_HAS_BOOLEAN(src_var)) {
		VAR_SET_BOOL(dst_var, src_var->var_val.boolean);
	} else if (VAR_HAS_INTEGER(src_var)) {
		VAR_SET_INT(dst_var, src_var->var_val.integer);
	} else if (VAR_HAS_STRING(src_var)) {
		strptr = ipc_stralloc(src_var->var_val.string);
		if (!strptr) {
			filebench_log(LOG_ERROR,
				"Cannot assign variable %s", name);
			return NULL;
		}
		VAR_SET_STR(dst_var, strptr);
	} else if (VAR_HAS_DOUBLE(src_var)) {
		VAR_SET_INT(dst_var, src_var->var_val.dbl_flt);
	} else if (VAR_HAS_RANDDIST(src_var))
		VAR_SET_RAND(dst_var, src_var->var_val.randptr);

	return dst_var;
}

var_t *
var_lvar_assign_boolean(char *name, boolean_t bool)
{
	var_t *var;

	var = var_lvar_alloc_local(name);
	if (!var) {
		filebench_log(LOG_ERROR, "Cannot assign variable %s",
			name);
		return NULL;
	}

	VAR_SET_BOOL(var, bool);

	return var;
}

var_t *
var_lvar_assign_integer(char *name, uint64_t integer)
{
	var_t *var;

	var = var_lvar_alloc_local(name);
	if (!var) {
		filebench_log(LOG_ERROR, "Cannot assign variable %s",
			name);
		return NULL;
	}

	VAR_SET_INT(var, integer);

	return var;
}

var_t *
var_lvar_assign_double(char *name, double dbl)
{
	var_t *var;

	var = var_lvar_alloc_local(name);

	if (!var) {
		filebench_log(LOG_ERROR, "Cannot assign variable %s",
						name);
		return NULL;
	}

	VAR_SET_DBL(var, dbl);

	return var;
}

var_t *
var_lvar_assign_string(char *name, char *string)
{
	var_t *var;
	char *strptr;

	var = var_lvar_alloc_local(name);
	if (!var) {
		filebench_log(LOG_ERROR, "Cannot assign variable %s",
					name);
		return NULL;
	}

	strptr = ipc_stralloc(string);
	if (!strptr) {
		filebench_log(LOG_ERROR, "Cannot assign variable %s",
				name);
		return NULL;
	}

	VAR_SET_STR(var, strptr);

	return var;
}

static var_t *
var_find_internal(var_t *var)
{
	char *n = fb_stralloc(var->var_name);
	char *name = n;
	var_t *rtn = NULL;

	name++;
	if (name[strlen(name) - 1] != '}')
		return NULL;
	name[strlen(name) - 1] = 0;

	if (!strncmp(name, STATS_VAR, strlen(STATS_VAR)))
		rtn = stats_findvar(var, name + strlen(STATS_VAR));

	if (!strcmp(name, EVENTGEN_VAR))
		rtn = eventgen_ratevar(var);

	if (!strcmp(name, DATE_VAR))
		rtn = date_var(var);

	if (!strcmp(name, SCRIPT_VAR))
		rtn = script_var(var);

	if (!strcmp(name, HOST_VAR))
		rtn = host_var(var);

	free(n);

	return rtn;
}

static var_t *
var_find_environment(var_t *var)
{
	char *n = fb_stralloc(var->var_name);
	char *name = n;
	char *strptr;

	name++;
	if (name[strlen(name) - 1] != ')') {
		free(n);
		return NULL;
	}

	name[strlen(name) - 1] = '\0';

	strptr = getenv(name);
	if (strptr) {
		VAR_SET_STR(var, strptr);
		free(n);
		return var;
	} else {
		free(n);
		return NULL;
	}
}

static var_t *
var_find_special(char *name)
{
	var_t *var = NULL;
	var_t *v = filebench_shm->shm_var_special_list;
	var_t *rtn;

	for (v = filebench_shm->shm_var_special_list; v; v = v->var_next) {
		if (!strcmp(v->var_name, name)) {
			var = v;
			break;
		}
	}

	if (!var)
		var = var_alloc_special(name);

	/* Internal system control variable */
	if (*name == '{') {
		rtn = var_find_internal(var);
		if (!rtn)
			filebench_log(LOG_ERROR,
			"Cannot find internal variable %s",
			var->var_name);
		return rtn;
	}

	/* Lookup variable in environment */
	if (*name == '(') {
		rtn = var_find_environment(var);
		if (!rtn)
			filebench_log(LOG_ERROR,
			"Cannot find environment variable %s",
			var->var_name);
		return rtn;
	}

	return NULL;
}

/*
 * replace the avd_t attribute value descriptor in the new FLOW_MASTER flowop
 * that points to a local variable with a new avd_t containing
 * the actual value from the local variable.
 */
void
avd_update(avd_t *avdp, var_t *lvar_list)
{
	/* Empty or not indirect, so no update needed */
	return;
}

void
var_update_comp_lvars(var_t *newlvar, var_t *proto_comp_vars,
			var_t *mstr_lvars)
{
	var_t *proto_lvar;

	/* find the prototype lvar from the inherited list */
	proto_lvar = var_find_list_only(newlvar->var_name, proto_comp_vars);

	if (!proto_lvar)
		return;

	/*
	 * if the new local variable has not already been assigned
	 * a value, try to copy a value from the prototype local variable
	 */
	if ((newlvar->var_type & VAR_TYPE_SET_MASK) == 0) {
		/* copy value from prototype lvar to new lvar */
		(void) var_copy(newlvar, proto_lvar);
	}
}
