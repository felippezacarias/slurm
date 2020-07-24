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
#include "src/slurmctld/node_scheduler.h"
#include "src/common/parse_time.h"
#include "src/common/power.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/burst_buffer.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/preempt.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/srun_comm.h"
#include "src/plugins/sched/colocation/colocation.h"

#ifndef COLOCATION_INTERVAL
#  define COLOCATION_INTERVAL	30
#endif

#define COLOCATION_LIMIT	100
#define DEFAULT_COLOCATION_FUNCTION       "optimal"
#define DEFAULT_COLOCATION_MODEL       	  "mlpregressor.sav"
#define DEFAULT_MODULE_NAME				  "degradation_model"
#define COLOCATION_SHARE				1
#define COLOCATION_EXCLUSIVE			0
// Only recompute the optimal pairs on new job arrival
#define DEFAULT_STATIC_COLOCATION_OPTIMAL_CHECK 0 

#define HARDWARE_COUNTER_STRING_SIZE 12056

/*********************** local variables *********************/
static bool stop_colocation = false;
static pthread_mutex_t term_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  term_cond = PTHREAD_COND_INITIALIZER;
static bool config_flag = false;
static int colocation_interval = COLOCATION_INTERVAL;
static int max_sched_job_cnt = COLOCATION_LIMIT;
static int sched_timeout = 0;
static double degradation_limit = -1.0;
static char *colocation_function = NULL;
static char *colocation_model = NULL;
static int check_combination;
static int fifo_shared;
PyObject *pModule;
PyObject *pFunc = NULL;
uint32_t priority = NO_VAL - 1;
List *colocated_jobs = NULL;

/*********************** local functions *********************/
static void _colocation_scheduling(void);
static void _attempt_colocation(void);
static void _load_config(void);
static void _my_sleep(int secs);
static void _compute_colocation_pairs(PyObject *pList);
static bool coalocate_candidate(struct job_record *job_ptr);
static int _find_job_by_id(uint32_t object, void *arg);
PyObject* _read_job_profile_file(struct job_record *job_ptr);
PyObject* _create_model_input(void);
static void _update_job_info(PyObject *pListColocation);
static void clean_colocated_jobs_list();


/* Terminate colocation_agent */
extern void stop_colocation_agent(void)
{
	slurm_mutex_lock(&term_lock);
	stop_colocation = true;
	slurm_cond_signal(&term_cond);
	slurm_mutex_unlock(&term_lock);
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

	if (sched_params && (tmp_ptr=strstr(sched_params, "max_colocation_sched=")))
		max_sched_job_cnt = atoi(tmp_ptr + 21);
	if (max_sched_job_cnt < 1) {
		error("Invalid SchedulerParameters max_colocation_sched: %d",
		      max_sched_job_cnt);
		max_sched_job_cnt = COLOCATION_LIMIT;
	}

	if (sched_params && (tmp_ptr=strstr(sched_params, "max_degradation=")))
		degradation_limit = atof(tmp_ptr + 16);
	if (degradation_limit < 0) {
		error("Invalid SchedulerParameters max_degradation: %.2f",
		      degradation_limit);
		degradation_limit = 100.0;
	}

	xfree(colocation_function);
	if (sched_params && (tmp_ptr = strstr(sched_params, "colocation_function="))) {
		colocation_function = xstrdup(tmp_ptr + 20);
		tmp_ptr = strchr(colocation_function, ',');
		if (tmp_ptr)
			tmp_ptr[0] = '\0';
	} else {
		colocation_function = xstrdup(DEFAULT_COLOCATION_FUNCTION);
	}
	fifo_shared = 0;
	if(((strcmp(colocation_function,"shared")) == 0)){
		fifo_shared = 1;
	}

	xfree(colocation_model);
	if (sched_params &&  (tmp_ptr = strstr(sched_params, "colocation_model="))) {
		colocation_model = xstrdup(tmp_ptr + 17);
		tmp_ptr = strchr(colocation_model, ',');
		if (tmp_ptr)
			tmp_ptr[0] = '\0';
	} else {
		colocation_model = xstrdup(DEFAULT_COLOCATION_MODEL);
	}

	if (sched_params &&  (tmp_ptr = strstr(sched_params, "colocation_check="))) {
		check_combination = atoi(tmp_ptr + 17);
		if (check_combination < 0) {
		error("Invalid SchedulerParameters colocation_check: %d",
		      check_combination);
		check_combination = DEFAULT_STATIC_COLOCATION_OPTIMAL_CHECK;
	}
	} else {
		check_combination = DEFAULT_STATIC_COLOCATION_OPTIMAL_CHECK;
	}

	debug5("COLOCATION: %s degradation_limit=%f max_colocation_sched=%d, colocation_model=%s, colocation_function=%s colocation_check=%d",
			__func__,degradation_limit,max_sched_job_cnt,colocation_model,colocation_function,check_combination);

	
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

static bool coalocate_candidate(struct job_record *job_ptr)
{
	uint32_t jobid;
	struct job_record *job_mate;
	bool candidate = false;
	ListIterator job_mate_iterator;

	// if it considers only job_pending we get an error that the reference
	// between two jobs can be missed and give a segfault on job purge
	// This will allow the colocation in batches
	if ((IS_JOB_PENDING(job_ptr)) &&
		((job_ptr->job_ptr_mate == NULL) ||
			(list_is_empty(job_ptr->job_ptr_mate))))
		return true;

	if (IS_JOB_RUNNING(job_ptr)){
		if((job_ptr->job_ptr_mate == NULL) ||
			(list_is_empty(job_ptr->job_ptr_mate)))
			candidate = true; 
		else{
			job_mate_iterator = list_iterator_create(job_ptr->job_ptr_mate);
			while ((jobid = (uint32_t) list_next(job_mate_iterator))) {
				if ((job_mate = find_job_record(jobid)) == NULL) {
					debug5("colocation: %s could not find job %u",__func__,jobid);
					candidate = true;
					continue;
				}
				//if they are running but in separate nodes
				if((IS_JOB_RUNNING(job_mate)) && 
				   (!bit_super_set(job_ptr->node_bitmap,job_mate->node_bitmap))){
					   candidate = true;
				//they are running together, became false and break the loop
				}else if((IS_JOB_RUNNING(job_mate)) && 
				   		(bit_super_set(job_ptr->node_bitmap,job_mate->node_bitmap))){
					   	candidate = false;
						break;
				}
			}
		}
	}

	return candidate;
}

static int _find_job_by_id(uint32_t object, void *arg)
{
	uint32_t job_info = object;
	uint32_t job_id          = *(uint32_t *)arg;

	if (job_info == job_id)
		return 1;

	return 0;
}

static void _colocation_scheduling(void)
{
	bool sched = false;
	
	uint32_t  jobs_to_colocate = 0;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	PyObject *pList = NULL;

	debug5("COLOCATION: %s",__func__);
	job_iterator = list_iterator_create(job_list);		
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (coalocate_candidate(job_ptr)){
			jobs_to_colocate++;
			//at update_job function we set job_ptr_mate to NULL or empty
			//so, if it is null the function plugin will recompute the mates 
			//everytime, otherwise only with new jobs
			if(job_ptr->job_ptr_mate == NULL){
				sched = true;
				debug5("COLOCATION: job_id %u mate_is_null",job_ptr->job_id); 
			}
			else{
				debug5("COLOCATION: job_id %u mate_is_empty %d count %u",
						job_ptr->job_id,list_is_empty(job_ptr->job_ptr_mate),
						list_count(job_ptr->job_ptr_mate)); 
			}
		}

		debug5("COLOCATION: job_id %u share_res %d state %u state_reason %u",
				job_ptr->job_id,job_ptr->details->share_res,
				job_ptr->job_state,job_ptr->state_reason); 
	}
	list_iterator_destroy(job_iterator);

	debug5("COLOCATION: %s jobs_to_colocate %u sched %d",__func__,jobs_to_colocate,sched); 


	if (sched)
		pList = _create_model_input();		
	
	if(pList != NULL){
		debug5("COLOCATION: function %s Model list input size %ld",__func__,PyList_GET_SIZE(pList));
		//Create degradation graph and compute colocation pairs
		_compute_colocation_pairs(pList);
		debug5("COLOCATION: %s After _compute_colocation_pairs!",__func__);
		Py_XDECREF(pList);

		//It possible may cause a job starvation, but for sure will increase the
		//wait time for a non shared job
		// TODO: Possible solution: tag the job as colocated.
		//_attempt_colocation();
		//debug5("COLOCATION: %s After _attempt_colocation!",__func__);

	}
	_attempt_colocation();
	debug5("COLOCATION: %s After _attempt_colocation!",__func__);
	clean_colocated_jobs_list();
	debug5("COLOCATION: %s After clean_colocated_jobs_list!",__func__);
}

static void _attempt_colocation(void)
{
	int rc = SLURM_FAILURE;
	uint32_t jobid;
	uint32_t  jobs_to_colocate = 0;
	struct job_record *job_ptr, *job_scan_ptr, *job_colocated;
	ListIterator job_iterator,job_mat_iterator;
	
	debug5("COLOCATION: %s",__func__);
	job_iterator = list_iterator_create(job_list);		
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if(jobs_to_colocate == max_sched_job_cnt) break;
		jobs_to_colocate++;
		//If the job_ptr was coalocated before, we dont test its mates 
		job_scan_ptr = list_find_first(colocated_jobs, _find_job_by_id,&job_ptr->job_id);
		if (!IS_JOB_RUNNING(job_ptr) ||
			(list_is_empty(job_ptr->job_ptr_mate)) ||
			((job_scan_ptr) && !(fifo_shared)))
			continue;
		
		job_mat_iterator = list_iterator_create(job_ptr->job_ptr_mate);
		while ((jobid = (uint32_t) list_next(job_mat_iterator)) &&
				(rc != SLURM_SUCCESS)) {			
			if ((job_scan_ptr = find_job_record(jobid)) == NULL) {
				debug5("colocation: %s could not find job %u",__func__,jobid);
				continue;
			}
			//When can I clean the colocated_jobs list?
			//Periodicaly clean the list later. Create func
			job_colocated = list_find_first(colocated_jobs, _find_job_by_id,&jobid);
			if(job_colocated)
				continue;

			if (IS_JOB_RUNNING(job_scan_ptr)){
				list_append(colocated_jobs,job_scan_ptr->job_id);
				continue;
			}

			debug5("COLOCATION: %s Trying colocate job %u",__func__,job_scan_ptr->job_id);

			rc = select_nodes(job_scan_ptr, false, NULL, NULL, false);

			if (rc == SLURM_SUCCESS) {
				/* job initiated */
				char job_id_str[64];			
				debug5("COLOCATION: %s Started %s in %s on %s",__func__,
					jobid2fmt(job_scan_ptr, job_id_str, sizeof(job_id_str)),
					job_scan_ptr->part_ptr->name, job_scan_ptr->nodes);
				power_g_job_start(job_scan_ptr);
				if (job_scan_ptr->batch_flag == 0)
					srun_allocate(job_scan_ptr->job_id);
				else if (
		#ifdef HAVE_BG
					/*
					* On a bluegene system we need to run the prolog
					* while the job is CONFIGURING so this can't work
					* off the CONFIGURING flag as done elsewhere.
					*/
					!job_scan_ptr->details ||
					!job_scan_ptr->details->prolog_running
		#else
					!IS_JOB_CONFIGURING(job_scan_ptr)
		#endif
					)
					launch_job(job_scan_ptr);

					// Save colocated job on tagged list
					list_append(colocated_jobs,job_scan_ptr->job_id);
			} else {
				debug5("COLOCATION: %s Failed to start JobId=%u: %s",__func__,
					job_scan_ptr->job_id, slurm_strerror(rc));
			}

		}	
		list_iterator_destroy(job_mat_iterator);

	}
	list_iterator_destroy(job_iterator);
}


static void clean_colocated_jobs_list(){
	struct job_record *job_ptr, *job_scan_ptr, *job_colocated;
	ListIterator job_iterator,colocated_iterator;
	
	debug5("COLOCATION: %s",__func__);
	job_iterator = list_iterator_create(job_list);		
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		colocated_iterator = list_iterator_create(colocated_jobs);
		while ((job_scan_ptr = (uint32_t) list_next(colocated_iterator))) {
			if(job_scan_ptr < job_ptr->job_id){
				list_delete_item(colocated_iterator);
			}
			else
			{
				break;
			}
		}
		list_iterator_destroy(colocated_iterator);
		//I just need the lowest jobid;
		break;
	}
	list_iterator_destroy(job_iterator);
}

PyObject* _create_model_input(void)
{
	PyObject *pList = NULL, *pProfileList;
	PyObject *pTuple, *pValue;
	struct job_record *job_ptr = NULL;
	ListIterator job_iterator;
	int rc = 0;
	double job_id;
	uint32_t job_coaloc_limit = 0;

	debug5("COLOCATION: %s Initiated.",__func__);

	//Allocating empty list
	pList = PyList_New(0);
	if(pList == NULL){
		debug("COLOCATION: %s Couldn't allocate Model input list",__func__);
		return NULL;
	}

	job_iterator = list_iterator_create(job_list);
	debug5("COLOCATION: %s Creating list",__func__);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		debug5("COLOCATION: %s Job_id %u  job_state %u job_coaloc %u",
				__func__,job_ptr->job_id,job_ptr->job_state,job_coaloc_limit);
		if(job_coaloc_limit == max_sched_job_cnt) break;
		if (coalocate_candidate(job_ptr)){
			job_coaloc_limit++;
			debug5("COLOCATION: %s Freeing job_mate_list job_id %u",__func__,job_ptr->job_id);
			FREE_NULL_LIST(job_ptr->job_ptr_mate);
			pTuple = PyTuple_New(2);
			//Adding info to de model as [(job_id,[perf_counters]),...]
			job_id = job_ptr->job_id * 1.0f;
			debug5("COLOCATION: %s PyFloat_FromDouble size %ld job_id %f",
				__func__,PyTuple_Size(pTuple),job_id);

			pValue = PyFloat_FromDouble(job_id);
			debug5("COLOCATION: %s after PyFloat_FromDouble size %ld job_id %f",
					__func__,PyTuple_Size(pTuple),job_id);					
            rc = PyTuple_SetItem(pTuple, 0, pValue);

			debug5("COLOCATION: %s _read_job_profile_file ",__func__);
			pProfileList = _read_job_profile_file(job_ptr);
			debug5("COLOCATION: %s job_id %u profile_list_size %ld",
					__func__,job_ptr->job_id,PyList_GET_SIZE(pProfileList)); 
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
	if(job_coaloc_limit == 1){
		debug5("COLOCATION: %s only one job. Don't create a model!",__func__);
		Py_XDECREF(pList);
		pList = NULL;
	}
	else
		debug5("COLOCATION: %s List Created",__func__);

	return pList;
}

/*Return List of performance counters on the file*/
PyObject* _read_job_profile_file(struct job_record *job_ptr)
{
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

static void _update_job_info(PyObject *pListColocation){
    PyObject *pValue;
	int i, j, ngroups, colocation;
	uint32_t job_id;
	struct job_record *job_ptr = NULL;
	struct job_record *job_ptr_sec = NULL;

	debug5("colocation: %s initiated.",__func__);

	ngroups = PyList_GET_SIZE(pListColocation);
	debug5("colocation: %s ngroups = %d.",__func__,ngroups);
	for(i = 0; i < ngroups; i++ ){
		pValue = PyList_GetItem(pListColocation,i);
		colocation = PyList_GET_SIZE(pValue);
		if(colocation > 1){
			
			job_id = (uint32_t) PyFloat_AsDouble(PyList_GetItem(pValue,0));
			if ((job_ptr = find_job_record(job_id)) == NULL) {
				debug5("colocation: %s could not find job %u",__func__,job_id);
			}
			if(job_ptr->job_ptr_mate == NULL){
				//Create list with null, because when removing an item it will not be deallocated
				job_ptr->job_ptr_mate = list_create(NULL);
			}
			job_ptr->details->share_res = COLOCATION_SHARE;

			for(j = 1; j < colocation; j++ ){
				job_id = (uint32_t) PyFloat_AsDouble(PyList_GetItem(pValue,j));
				if ((job_ptr_sec = find_job_record(job_id)) == NULL) {
					debug5("colocation: %s could not find job %u",__func__,job_id);
				}
				
				list_append(job_ptr->job_ptr_mate,job_ptr_sec->job_id);

				// For optimal the jobid appears only once in the final result
				// from the python. Also included DIO, but it is not required
				if(((strcmp(colocation_function,DEFAULT_COLOCATION_FUNCTION)) == 0) ||
					((strcmp(colocation_function,"dio")) == 0)){
					if(job_ptr_sec->job_ptr_mate == NULL){
						job_ptr_sec->job_ptr_mate = list_create(NULL);
						job_ptr_sec->details->share_res = COLOCATION_SHARE;
					}
					list_append(job_ptr_sec->job_ptr_mate,job_ptr->job_id);
				}
				
				debug5("colocation: %s Jobs to share node jobid1 = %u jobid2 = %u",
						__func__,job_ptr->job_id,job_ptr_sec->job_id);
			}		
		}
		else{

			job_id = (uint32_t) PyFloat_AsDouble(PyList_GetItem(pValue,0));
			if ((job_ptr = find_job_record(job_id)) == NULL) {
				debug5("colocation: %s could not find job %u",__func__,job_id);
			}

			job_ptr->details->share_res = COLOCATION_EXCLUSIVE;
			// If job_id_mat is NULL, the plugin will always check if a new 
			// combination is possible with previous jobs
			if(check_combination)
				FREE_NULL_LIST(job_ptr->job_ptr_mate);
			else
				//If it's empty, the plugin will only check new combinations when new jobs arrive.
				job_ptr->job_ptr_mate = list_create(NULL);

			debug5("colocation: %s Jobs to execute alone jobid = %u",
					__func__,job_ptr->job_id);
		}
	}
}

static void _compute_colocation_pairs(PyObject *pList)
{
    PyObject *pArgs, *pValue, *pValue2;

	debug5("Colocation: %s Initiated input size %ld.",__func__,PyList_GET_SIZE(pList) );

    if (pModule != NULL) {
        if(pFunc == NULL) pFunc = PyObject_GetAttrString(pModule, colocation_function);
        /* pFunc is a new reference */

        if (pFunc && PyCallable_Check(pFunc)) {
            pArgs = PyTuple_New(3);
			//Setting hardware counters list
			PyTuple_SetItem(pArgs, 0, pList);
			
			debug5("Colocation: %s pList size %ld.",__func__,PyList_GET_SIZE(pList) );
			pValue2 = PyList_GetItem(pList,0);
			debug5("Colocation[0]: %s is tuple %ld.",__func__,PyTuple_Check(pValue2));
			debug5("Colocation[0]: %s tuple value_1 %f tuple list size %d",
					__func__,PyFloat_AsDouble(PyTuple_GetItem(pValue2,0)),
					PyList_GET_SIZE(PyTuple_GetItem(pValue2,1)));

			pValue2 = PyList_GetItem(pList,1);
			debug5("Colocation[1]: %s is tuple %ld.",__func__,PyTuple_Check(pValue2));
			debug5("Colocation[1]: %s tuple value_1 %f tuple list size %d",
					__func__,PyFloat_AsDouble(PyTuple_GetItem(pValue2,0)),
					PyList_GET_SIZE(PyTuple_GetItem(pValue2,1)));


			//Setting degradation limit to colocate jobs
			PyTuple_SetItem(pArgs, 1, PyFloat_FromDouble(degradation_limit));

			//Setting model to be used to colocate jobs
			PyTuple_SetItem(pArgs, 2, PyString_FromString(colocation_model));

			debug5("COLOCATION: function %s calling PyObject_CallObject",__func__);
			pValue = PyObject_CallObject(pFunc, pArgs);
            if (pValue != NULL) {
				if (pValue == Py_None){
					debug5("Colocation: %s after PyObject_CallObject result is [%s]",
							__func__,PyString_AsString(PyObject_Str(pValue)));
					return;
				} 
				debug5("Colocation: %s after PyObject_CallObject value is_list %d value size %ld type_res %s",
						__func__,PyList_Check(pValue),PyList_GET_SIZE(pValue),
						PyString_AsString(PyObject_Str(pValue)));

				_update_job_info(pValue);


				Py_DECREF(pArgs);
                Py_DECREF(pValue);
				Py_XDECREF(pList);
            }
            else {
                PyErr_Print();
				PyErr_Clear();
				debug5("Colocation: %s Call failed!",__func__);
            }
        }
        else {
            if (PyErr_Occurred())
                PyErr_Print();
			debug5("Colocation: %s Cannot find function colocation_pairs!",__func__);
        }
    }

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
	PyObject *pName, *pFunc;



	_load_config();

	Py_Initialize();
    pName = PyString_FromString(DEFAULT_MODULE_NAME); 

	pModule = PyImport_Import(pName);
    Py_DECREF(pName);
    if (pModule == NULL) {
        PyErr_Print();
		debug5("Colocation: %s Failed to load degradation_model!",__func__);
	}

	// Initiate colocated tag list
	colocated_jobs = list_create(NULL);
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
	FREE_NULL_LIST(colocated_jobs);
	xfree(colocation_function);
	xfree(colocation_model);
    Py_XDECREF(pModule);
	Py_XDECREF(pFunc);
	Py_Finalize();
	return NULL;
}