/*****************************************************************************\
 *  colocation.c - Contention aware plugin.
 *		Periodically create a degradation graph to calculate the
 *		optimal set with minimum performance degradation.
 *****************************************************************************
 *  
 *  Produced as my master thesis research.
 *  Written by Felippe Zacarias <fvzacarias@gmail.com> et. al.
 *  
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <Python.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/parse_time.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/plugins/sched/colocation/colocation.h"

#ifndef COLOCATION_INTERVAL
#  define COLOCATION_INTERVAL	30
#endif

#define HARDWARE_COUNTER_STRING_SIZE 2056

/*********************** local variables *********************/
static bool stop_colocation = false;
static pthread_mutex_t term_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  term_cond = PTHREAD_COND_INITIALIZER;
static bool config_flag = false;
static int colocation_interval = COLOCATION_INTERVAL;
static int max_sched_job_cnt = 50;
static int sched_timeout = 0;
static List colocation_job_list = (List) NULL;


struct colocation_pairs {
	uint32_t job_id;
	//Main job
	struct job_record *job_ptr_p;
	//Colocated job
	struct job_record *job_ptr_s;
};

/*********************** local functions *********************/
static void _colocation_scheduling(void);
static void _load_config(void);
static void _my_sleep(int secs);
static void _compute_colocation_pairs(void);
static int _suspend_job(uint32_t job_id);
PyObject* _read_job_profile_file(struct job_record *job_ptr);
PyObject* _create_model_input(void);



/* Terminate colocation_agent */
extern void stop_colocation_agent(void)
{
	slurm_mutex_lock(&term_lock);
	stop_colocation = true;
	slurm_cond_signal(&term_cond);
	slurm_mutex_unlock(&term_lock);
}

static void _colocation_job_list_del(void *x)
{
	xfree(x);
}


static void _my_sleep(int secs)
{
	struct timespec ts = {0, 0};
	struct timeval now;

	gettimeofday(&now, NULL);
	ts.tv_sec = now.tv_sec + secs;
	ts.tv_nsec = now.tv_usec * 1000;
	slurm_mutex_lock(&term_lock);
	if (!stop_colocation)
		slurm_cond_timedwait(&term_cond, &term_lock, &ts);
	slurm_mutex_unlock(&term_lock);
}

static void _load_config(void)
{
	char *sched_params, *select_type, *tmp_ptr;

	sched_timeout = slurm_get_msg_timeout() / 2;
	sched_timeout = MAX(sched_timeout, 1);
	sched_timeout = MIN(sched_timeout, 10);

	sched_params = slurm_get_sched_params();

	if (sched_params && (tmp_ptr=strstr(sched_params, "interval=")))
		colocation_interval = atoi(tmp_ptr + 9);
	if (colocation_interval < 1) {
		error("Invalid SchedulerParameters interval: %d",
		      colocation_interval);
		colocation_interval = COLOCATION_INTERVAL;
	}

	if (sched_params && (tmp_ptr=strstr(sched_params, "max_job_bf=")))
		max_sched_job_cnt = atoi(tmp_ptr + 11);
	if (sched_params && (tmp_ptr=strstr(sched_params, "bf_max_job_test=")))
		max_sched_job_cnt = atoi(tmp_ptr + 16);
	if (max_sched_job_cnt < 1) {
		error("Invalid SchedulerParameters bf_max_job_test: %d",
		      max_sched_job_cnt);
		max_sched_job_cnt = 50;
	}
	xfree(sched_params);

	select_type = slurm_get_select_type();
	if (!xstrcmp(select_type, "select/serial")) {
		/* Do not spend time computing expected start time for
		 * pending jobs */
		max_sched_job_cnt = 0;
		stop_colocation_agent();
	}
	xfree(select_type);
}

static int _suspend_job(uint32_t job_id)
{
	int rc;
	suspend_msg_t msg;

	msg.job_id = job_id;
	msg.job_id_str = NULL;
	msg.op = SUSPEND_JOB;
	rc = job_suspend(&msg, 0, -1, false, NO_VAL16);
	/* job_suspend() returns ESLURM_DISABLED if job is already suspended */
	if (rc == SLURM_SUCCESS) {
		if (slurmctld_conf.debug_flags)
			info("colocation: suspending JobID=%u", job_id);
		else
			debug("colocation: suspending JobID=%u", job_id);
	} else if (rc != ESLURM_DISABLED) {
		info("colocation: suspending JobID=%u: %s",
		     job_id, slurm_strerror(rc));
	}
	return rc;
}

static void _colocation_scheduling(void)
{
	int j, rc = SLURM_SUCCESS, job_cnt = 0, jobs_to_colocate = 0;
	List job_queue;
	job_queue_rec_t *job_queue_rec;
	List preemptee_candidates = NULL;
	struct job_record *job_ptr = NULL;
	struct part_record *part_ptr;
	bitstr_t *alloc_bitmap = NULL, *avail_bitmap = NULL;
	bitstr_t *exc_core_bitmap = NULL;
	uint32_t max_nodes, min_nodes, req_nodes, time_limit;
	time_t now = time(NULL), sched_start, last_job_alloc;
	bool resv_overlap = false;
	ListIterator job_iterator;
	PyObject *pList = NULL;


	debug5("COLOCATION: %s colocation_job_list count = %d is empty = %d.",__func__,list_count(colocation_job_list),list_is_empty(colocation_job_list));
	//First entrance or 
	//if(list_is_empty(colocation_job_list)){
	//	job_iterator = list_iterator_create(job_list);		
	//	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
	//		////Counting number of jobs that actualy can be scheduled
	//		if(job_ptr->job_state == 0 || job_ptr->job_state == 1) jobs_to_colocate++;
	//		if(job_ptr->job_state == 1){
	//			debug5("COLOCATION: SUSPENDING job_id %d",job_ptr->job_id);
	//			_suspend_job(job_ptr->job_id);
	//		}
	//	}
	//	list_iterator_destroy(job_iterator);
	//}

	//Create degradation graph and compute colocation pairs
	_compute_colocation_pairs();


	debug5("COLOCATION: %s job_list count = %d is empty = %d.",__func__,list_count(job_list),list_is_empty(job_list));

	pList = _create_model_input();
	debug5("COLOCATION: function %s pList size %d",__func__,PyList_GET_SIZE(pList));
	PyObject *tupla;
	if(pList != NULL || PyList_GET_SIZE(pList) > 0){
		tupla = PyList_GetItem(pList,0);
		debug5("COLOCATION: function %s Tupla[1] = %f size [2] = %d ",__func__,PyFloat_AsDouble(PyTuple_GetItem(tupla,0)),PyList_GET_SIZE(PyTuple_GetItem(tupla,1)));
		Py_DECREF(tupla);
	}
	Py_XDECREF(pList);


	//Testing how to get all the jobs, running or not
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		debug5("COLOCATION: function %s jobid %d job_status %d share_res %d priority %d.",__func__,job_ptr->job_id,job_ptr->job_state,job_ptr->details->share_res,job_ptr->priority);
		//if(job_ptr->job_state == 1) _suspend_job(job_ptr->job_id);
		
		//if(job_ptr->job_id == 115 || job_ptr->job_id == 116 ){
		//	//rc = job_requeue(0, job_ptr->job_id, NULL, true, 0);
		//	if(job_ptr->job_state != 1 && job_ptr->priority != 0){
		//		suspend_msg_t msg;
		//		debug5("COLOCATION: SUSPENDING job_id %d",job_ptr->job_id);
		//		msg.job_id = job_ptr->job_id;
		//		msg.job_id_str = NULL;
		//		msg.op = SUSPEND_JOB;
		//		rc = job_suspend(&msg, 0, -1, false, NO_VAL16);
//
		//		job_ptr->priority = 0;
		//		job_ptr->details->share_res = 1;
		//	}
		//	else{
		//		if(job_ptr->priority == 0){
		//			suspend_msg_t msg;
		//			debug5("COLOCATION: RESUMING job_id %d",job_ptr->job_id);
		//			msg.job_id = job_ptr->job_id;
		//			msg.job_id_str = NULL;
		//			msg.op = RESUME_JOB;
		//			//rc = job_suspend(&msg, 0, -1, false, NO_VAL16);
		//			rc = job_requeue(0, job_ptr->job_id, NULL, true, 0);
//
		//			job_ptr->priority = 100;
		//			job_ptr->details->share_res = 1;
		//		}
//
		//	}
		//}
		//if(job_ptr->job_state == 2) rc = job_requeue(0, job_ptr->job_id, NULL, true, 0);
	}
	list_iterator_destroy(job_iterator);

	sched_start = now;
	last_job_alloc = now - 1;
	alloc_bitmap = bit_alloc(node_record_count);
	job_queue = build_job_queue(true, false);
	sort_job_queue(job_queue);
	while ((job_queue_rec = (job_queue_rec_t *) list_pop(job_queue))) {
		job_ptr  = job_queue_rec->job_ptr;
		part_ptr = job_queue_rec->part_ptr;
		xfree(job_queue_rec);

		debug5("COLOCATION: function %s jobid %d hardware profile %s.",__func__,job_ptr->job_id,job_ptr->hwprofile);

		if (part_ptr != job_ptr->part_ptr)
			continue;	/* Only test one partition */

		if (job_cnt++ > max_sched_job_cnt) {
			debug2("scheduling loop exiting after %d jobs",
			       max_sched_job_cnt);
			break;
		}

		/* Determine minimum and maximum node counts */
		/* On BlueGene systems don't adjust the min/max node limits
		   here.  We are working on midplane values. */
		min_nodes = MAX(job_ptr->details->min_nodes,
				part_ptr->min_nodes);

		if (job_ptr->details->max_nodes == 0)
			max_nodes = part_ptr->max_nodes;
		else
			max_nodes = MIN(job_ptr->details->max_nodes,
					part_ptr->max_nodes);

		max_nodes = MIN(max_nodes, 500000);     /* prevent overflows */

		if (job_ptr->details->max_nodes)
			req_nodes = max_nodes;
		else
			req_nodes = min_nodes;

		if (min_nodes > max_nodes) {
			/* job's min_nodes exceeds partition's max_nodes */
			continue;
		}

		j = job_test_resv(job_ptr, &now, true, &avail_bitmap,
				  &exc_core_bitmap, &resv_overlap, false);
		if (j != SLURM_SUCCESS) {
			FREE_NULL_BITMAP(avail_bitmap);
			FREE_NULL_BITMAP(exc_core_bitmap);
			continue;
		}

		rc = select_g_job_test(job_ptr, avail_bitmap,
				       min_nodes, max_nodes, req_nodes,
				       SELECT_MODE_WILL_RUN,
				       preemptee_candidates, NULL,
				       exc_core_bitmap);
		if (rc == SLURM_SUCCESS) {
			last_job_update = now;
			if (job_ptr->time_limit == INFINITE)
				time_limit = 365 * 24 * 60 * 60;
			else if (job_ptr->time_limit != NO_VAL)
				time_limit = job_ptr->time_limit * 60;
			else if (job_ptr->part_ptr &&
				 (job_ptr->part_ptr->max_time != INFINITE))
				time_limit = job_ptr->part_ptr->max_time * 60;
			else
				time_limit = 365 * 24 * 60 * 60;
			if (bit_overlap(alloc_bitmap, avail_bitmap) &&
			    (job_ptr->start_time <= last_job_alloc)) {
				job_ptr->start_time = last_job_alloc;
			}
			bit_or(alloc_bitmap, avail_bitmap);
			last_job_alloc = job_ptr->start_time + time_limit;
		}
		FREE_NULL_BITMAP(avail_bitmap);
		FREE_NULL_BITMAP(exc_core_bitmap);

		if ((time(NULL) - sched_start) >= sched_timeout) {
			debug2("scheduling loop exiting after %d jobs",
			       max_sched_job_cnt);
			break;
		}
	}
	FREE_NULL_LIST(job_queue);
	FREE_NULL_BITMAP(alloc_bitmap);
}

PyObject* _create_model_input(void){
	PyObject *pList,*pProfileList;
	PyObject *pTuple, *pValue;
	struct job_record *job_ptr = NULL;
	ListIterator job_iterator;
	int rc = 0;
	double id;

	debug5("COLOCATION: %s",__func__);

	//Allocating empty list
	pList = PyList_New(0);
	if(pList == NULL){
		debug("COLOCATION: %s Couldn't allocate Model input list",__func__);
		return NULL;
	}

	job_iterator = list_iterator_create(job_list);
	debug5("COLOCATION: %s Creating list",__func__);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if(job_ptr->job_state == JOB_PENDING || 
			job_ptr->job_state == JOB_SUSPENDED ||
			job_ptr->job_state == JOB_RUNNING ){
			pTuple = PyTuple_New(2);
			//Adding info to de model as [(job_id,[perf_counters]),...]
			id = job_ptr->job_id * 1.0f;
			debug5("COLOCATION: %s PyFloat_FromDouble size %d job_id %f",__func__,PyTuple_Size(pTuple),id);
			pValue = PyFloat_FromDouble(id);
			debug5("COLOCATION: %s after PyFloat_FromDouble size %d job_id %f",__func__,PyTuple_Size(pTuple),id);
            rc = PyTuple_SetItem(pTuple, 0, pValue);
			debug5("COLOCATION: %s _read_job_profile_file ",__func__);
			pProfileList = _read_job_profile_file(job_ptr);
			PyTuple_SetItem(pTuple, 1, pProfileList);

			debug5("COLOCATION: %s PyList_Append ",__func__);
			rc = PyList_Append(pList,pTuple);
			if(rc != 0 ){
				debug5("COLOCATION: %s tupla append error.",__func__);
				return NULL;
			}
		}
	}
	list_iterator_destroy(job_iterator);
	debug5("COLOCATION: %s List Created",__func__);
	return pList;
}

/*Return List of performance counters on the file*/
PyObject* _read_job_profile_file(struct job_record *job_ptr){
	PyObject *pList;
	FILE *fp;
	const char separator[2]=",";
	char *token;
	char line[HARDWARE_COUNTER_STRING_SIZE];
	double value;
	int rc = 0;

	debug5("COLOCATION: %s",__func__);

	pList = PyList_New(0);
	if(pList == NULL){
		debug("COLOCATION: %s Couldn't allocate Performance counter list",__func__);
		return NULL;
	}

	//Investigate why the plugin isn't able to read it from /home/user.....
	fp = fopen(job_ptr->hwprofile,"r");
	if(fp == NULL){
		debug5("COLOCATION: %s couldn't open file %s errno = %d",__func__,job_ptr->hwprofile,errno);
		return NULL;
	}

	while(fgets(line, sizeof line, fp) != NULL){
		token = strtok(line, separator);
		while (token != NULL)
		{
			//debug5("COLOCATION: %s token read %s.",__func__,token);
			value = atof(token);
			rc = PyList_Append(pList,PyFloat_FromDouble(value));
			if(rc != 0 ){
				debug5("COLOCATION: %s token %s append error.",__func__,token);
				fclose(fp);
				return NULL;
			}			
			token = strtok (NULL,separator);
		}
	}

	fclose(fp);
	return pList;
}

static void _compute_colocation_pairs(void){
	PyObject *pName, *pModule, *pFunc;
    PyObject *pArgs, *pValue;
    int i;

	debug5("Colocation: %s Initiated.",__func__);
    Py_Initialize();
    pName = PyString_FromString("degradation_model");
    /* Error checking of pName left out */

    pModule = PyImport_Import(pName);
    Py_DECREF(pName);

    if (pModule != NULL) {
        pFunc = PyObject_GetAttrString(pModule, "colocation_pairs");
        /* pFunc is a new reference */

        if (pFunc && PyCallable_Check(pFunc)) {
            pArgs = PyTuple_New(2);
            pValue = PyInt_FromLong(20);
            /* pValue reference stolen here: */
            PyTuple_SetItem(pArgs, 0, pValue);
			pValue = PyInt_FromLong(40);
            PyTuple_SetItem(pArgs, 1, pValue);

            
            pValue = PyObject_CallObject(pFunc, pArgs);
            Py_DECREF(pArgs);
            if (pValue != NULL) {
				debug5("Colocation: %s Result of call: %ld!",__func__,PyInt_AsLong(pValue));
                Py_DECREF(pValue);
            }
            else {
                Py_DECREF(pFunc);
                Py_DECREF(pModule);
                PyErr_Print();
				debug5("Colocation: %s Call failed!",__func__);
            }
        }
        else {
            if (PyErr_Occurred())
                PyErr_Print();
			debug5("Colocation: %s Cannot find function colocation_pairs!",__func__);            
        }
        Py_XDECREF(pFunc);
        Py_DECREF(pModule);
    }
    else {
        PyErr_Print();
		debug5("Colocation: %s Failed to load degradation_model!",__func__);
    }
    Py_Finalize();
}

/* Note that slurm.conf has changed */
extern void colocation_reconfig(void)
{
	config_flag = true;
}

/* colocation_agent - detached thread periodically when pending jobs can start */
extern void *colocation_agent(void *args)
{
	time_t now;
	double wait_time;
	static time_t last_sched_time = 0;
	/* Read config, nodes and partitions; Write jobs */
	slurmctld_lock_t all_locks = {
		READ_LOCK, WRITE_LOCK, READ_LOCK, READ_LOCK, READ_LOCK };

	_load_config();

	//Initializing colocation structure
	FREE_NULL_LIST(colocation_job_list);
	colocation_job_list = list_create(NULL);

	last_sched_time = time(NULL);
	while (!stop_colocation) {
		_my_sleep(colocation_interval);
		if (stop_colocation)
			break;
		if (config_flag) {
			config_flag = false;
			_load_config();
		}
		now = time(NULL);
		wait_time = difftime(now, last_sched_time);
		if ((wait_time < colocation_interval))
			continue;

		lock_slurmctld(all_locks);
		_colocation_scheduling();
		last_sched_time = time(NULL);
		(void) bb_g_job_try_stage_in();
		unlock_slurmctld(all_locks);
	}
	FREE_NULL_LIST(colocation_job_list);
	return NULL;
}
