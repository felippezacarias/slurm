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
bool first_time = true;
uint32_t priority = NO_VAL - 1;
uint32_t previous_jobs_to_colocate = 0;



struct colocation_pairs {
	uint32_t job_id;
	//Main job
	struct job_record *job_ptr_p;
	//Colocated job
	struct job_record *job_ptr_s;
};

/*********************** local functions *********************/
static void _colocation_scheduling_dynamic(void);
static void _colocation_scheduling_static(void);
static void _load_config(void);
static void _my_sleep(int secs);
static void _compute_colocation_pairs(PyObject *pList);
static int _suspend_job(uint32_t job_id);
static void _resume_job(uint32_t job_id);
PyObject* _read_job_profile_file(struct job_record *job_ptr);
PyObject* _create_model_input(void);
static void _update_job_info(PyObject *pListColocation);



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

static void _resume_job(uint32_t job_id)
{
	int rc;
	suspend_msg_t msg;

	msg.job_id = job_id;
	msg.job_id_str = NULL;
	msg.op = RESUME_JOB;
	rc = job_suspend(&msg, 0, -1, false, NO_VAL16);
	if (rc == SLURM_SUCCESS) {
		if (slurmctld_conf.debug_flags)
			info("colocation: resuming JobID=%u", job_id);
		else
			debug("colocation: resuming JobID=%u", job_id);
	} else if (rc != ESLURM_ALREADY_DONE) {
		error("colocation: resuming JobID=%u: %s",
		      job_id, slurm_strerror(rc));
	}
}

static void _colocation_scheduling_dynamic(void)
{
	int j, rc = SLURM_SUCCESS, job_cnt = 0;
	uint32_t jobs_to_colocate = 0;
	List job_queue;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	job_queue_rec_t *job_queue_rec;
	PyObject *pList = NULL;
	bool is_executing = false;


	debug5("COLOCATION: %s colocation_job_list count = %d is empty = %d.",__func__,list_count(colocation_job_list),list_is_empty(colocation_job_list));

	debug5("COLOCATION: %s entrou",__func__);
	job_iterator = list_iterator_create(job_list);		
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if(job_ptr->job_state == JOB_PENDING) jobs_to_colocate++;
		
		if(job_ptr->job_state == JOB_RUNNING) is_executing = true;
					
		debug5("COLOCATION: job_id %u priority %u share_res %d state_reason %u",job_ptr->job_id,job_ptr->priority,job_ptr->details->share_res,job_ptr->state_reason); 
	}
	list_iterator_destroy(job_iterator);

	if((jobs_to_colocate >= 1)){
		if (jobs_to_colocate % 2 == 0){
			pList = _create_model_input();
			debug5("COLOCATION: function %s Model list input size %d",__func__,PyList_GET_SIZE(pList));
		}
		else{
			if(!is_executing){
				job_iterator = list_iterator_create(job_list);		
				while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
					if(job_ptr->job_state == JOB_PENDING){
						job_ptr->priority = priority;
						first_time = true;
						break;
					}
				}
				debug5("COLOCATION: job_id %u priority %u share_res %d state_reason %u",job_ptr->job_id,job_ptr->priority,job_ptr->details->share_res,job_ptr->state_reason); 
			}
		list_iterator_destroy(job_iterator);
		}
	}

	if(pList != NULL){
		//Create degradation graph and compute colocation pairs
		_compute_colocation_pairs(pList);
	
		Py_XDECREF(pList);
	}
}

static void _colocation_scheduling_static(void)
{
	int j, rc = SLURM_SUCCESS, job_cnt = 0;
	uint32_t jobs_to_colocate = 0;
	List job_queue;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	job_queue_rec_t *job_queue_rec;
	PyObject *pList = NULL;

	debug5("COLOCATION: %s colocation_job_list count = %d is empty = %d.",__func__,list_count(colocation_job_list),list_is_empty(colocation_job_list));
	if(first_time){
		debug5("COLOCATION: %s entrou first time",__func__);
		job_iterator = list_iterator_create(job_list);		
		while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
			if(job_ptr->job_state == JOB_PENDING) jobs_to_colocate++;

			debug5("COLOCATION: job_id %u priority %u share_res %d state_reason %u",job_ptr->job_id,job_ptr->priority,job_ptr->details->share_res,job_ptr->state_reason); 
		}
		list_iterator_destroy(job_iterator);
		//Rever essa questão
		if (jobs_to_colocate != 0) first_time = false;
	}

	if((jobs_to_colocate >= 1)){
		if (jobs_to_colocate % 2 == 0){
			pList = _create_model_input();
			debug5("COLOCATION: function %s Model list input size %d",__func__,PyList_GET_SIZE(pList));
		}
		else{
			job_iterator = list_iterator_create(job_list);		
			while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
				if(job_ptr->job_state == JOB_PENDING){
					job_ptr->priority = priority;
					first_time = true;
					break;
				}
				debug5("COLOCATION: job_id %u priority %u share_res %d state_reason %u",job_ptr->job_id,job_ptr->priority,job_ptr->details->share_res,job_ptr->state_reason); 
			}
			list_iterator_destroy(job_iterator);
		}
	}
	//else{
	//	first_time = true;
	//}
	
	if(pList != NULL){
		//Create degradation graph and compute colocation pairs
		_compute_colocation_pairs(pList);
	
		Py_XDECREF(pList);
	}
}

PyObject* _create_model_input(void){
	PyObject *pList = NULL, *pProfileList;
	PyObject *pTuple, *pValue;
	struct job_record *job_ptr = NULL;
	ListIterator job_iterator;
	int rc = 0;
	double job_id;

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
		debug5("COLOCATION: %s Job_id %u  job_state %u ",__func__,job_ptr->job_id,job_ptr->job_state);
		if(job_ptr->job_state == JOB_PENDING){
			pTuple = PyTuple_New(2);
			//Adding info to de model as [(job_id,[perf_counters]),...]
			job_id = job_ptr->job_id * 1.0f;
			debug5("COLOCATION: %s PyFloat_FromDouble size %d job_id %f",__func__,PyTuple_Size(pTuple),job_id);
			pValue = PyFloat_FromDouble(job_id);
			debug5("COLOCATION: %s after PyFloat_FromDouble size %d job_id %f",__func__,PyTuple_Size(pTuple),job_id);
            rc = PyTuple_SetItem(pTuple, 0, pValue);
			debug5("COLOCATION: %s _read_job_profile_file ",__func__);
			pProfileList = _read_job_profile_file(job_ptr);
			debug5("COLOCATION: %s job_id %u profile_list_size %d",__func__,job_ptr->job_id,PyList_GET_SIZE(pProfileList)); 
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

static void _update_job_info(PyObject *pListColocation){
    PyObject *pValue;
	int i, ngroups, colocation, rc = 0;
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

			job_id = (uint32_t) PyFloat_AsDouble(PyList_GetItem(pValue,1));
			if ((job_ptr_sec = find_job_record(job_id)) == NULL) {
				debug5("colocation: %s could not find job %u",__func__,job_id);
			}

			debug5("colocation: %s Jobs to share node jobid1 = %u jobid2 = %u priority = %u.",__func__,job_ptr->job_id,job_ptr_sec->job_id,priority);

			job_ptr->details->share_res = 1;
			job_ptr_sec->details->share_res = 1;
			job_ptr_sec->priority = priority;
			job_ptr->priority = priority; 
			priority--;
			
		}
		else{

			job_id = (uint32_t) PyFloat_AsDouble(PyList_GetItem(pValue,0));
			if ((job_ptr = find_job_record(job_id)) == NULL) {
				debug5("colocation: %s could not find job %u",__func__,job_id);
			}

			job_ptr->details->share_res = 0;
			job_ptr->priority = priority; 
			priority--;
			debug5("colocation: %s Jobs to execute alone jobid = %u priority %u.",__func__,job_ptr->job_id,priority);
		}
	}
}

static void _compute_colocation_pairs(PyObject *pList){
	PyObject *pName, *pModule, *pFunc;
    PyObject *pArgs, *pValue, *pValue2;
	//TODO: This limit can be passed through config variables
    double degradation_limit = 100.0;

	debug5("Colocation: %s Initiated input size %d.",__func__,PyList_GET_SIZE(pList) );
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
			//Setting hardware counters list
			PyTuple_SetItem(pArgs, 0, pList);
			
			debug5("Colocation: %s pList size %d.",__func__,PyList_GET_SIZE(pList) );
			pValue2 = PyList_GetItem(pList,0);
			debug5("Colocation[0]: %s is tuple %d.",__func__,PyTuple_Check(pValue2));
			debug5("Colocation[0]: %s tuple value_1 %f tuple list size %d",__func__,PyFloat_AsDouble(PyTuple_GetItem(pValue2,0)),PyList_GET_SIZE(PyTuple_GetItem(pValue2,1)));

			pValue2 = PyList_GetItem(pList,1);
			debug5("Colocation[1]: %s is tuple %d.",__func__,PyTuple_Check(pValue2));
			debug5("Colocation[1]: %s tuple value_1 %f tuple list size %d",__func__,PyFloat_AsDouble(PyTuple_GetItem(pValue2,0)),PyList_GET_SIZE(PyTuple_GetItem(pValue2,1)));


			//Setting degradation limit to colocate jobs
			PyTuple_SetItem(pArgs, 1, PyFloat_FromDouble(degradation_limit));


			debug5("COLOCATION: function %s calling PyObject_CallObject",__func__);
			pValue = PyObject_CallObject(pFunc, pArgs);
            Py_DECREF(pArgs);
            if (pValue != NULL) {
				debug5("Colocation: %s value is_list %d value size %d",__func__,PyList_Check(pValue),PyList_GET_SIZE(pValue));
				pValue2 = PyList_GetItem(pValue,0);
				debug5("Colocation: %s Result of call: %f",__func__,PyFloat_AsDouble(PyList_GetItem(pValue2,0)));
				debug5("Colocation: %s Result of call: %f",__func__,PyFloat_AsDouble(PyList_GetItem(pValue2,1)));
				_update_job_info(pValue);


				//pArgs = PyList_GetItem(pValue,0);
				//pValue2 = PyList_GetItem(pValue,1);
				//debug5("COLOCATION: function %s Tupla[1] = %f size [2] = %d ",__func__,PyFloat_AsDouble(PyTuple_GetItem(pValue,0)),PyList_GET_SIZE(PyTuple_GetItem(pValue,1)));
				//Py_DECREF(pArgs);
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
		_colocation_scheduling_static();
		last_sched_time = time(NULL);
		(void) bb_g_job_try_stage_in();
		unlock_slurmctld(all_locks);
	}
	FREE_NULL_LIST(colocation_job_list);
	return NULL;
}
