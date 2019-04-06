/*****************************************************************************\
 *  colocation_wrapper.c - Plugin for Slurm's contention awareness internal scheduler.
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

#include <stdio.h>

#include "slurm/slurm_errno.h"

#include "src/common/plugin.h"
#include "src/common/log.h"
#include "src/common/node_select.h"
#include "src/common/slurm_priority.h"
#include "src/slurmctld/job_scheduler.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/plugins/sched/colocation/colocation.h"

const char		plugin_name[]	= "Slurm Contention Aware Scheduler plugin";
const char		plugin_type[]	= "sched/colocation";
const uint32_t		plugin_version	= SLURM_VERSION_NUMBER;

static pthread_t colocation_thread = 0;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;

int init(void)
{
	verbose( "sched: Colocation scheduler plugin loaded" );

	slurm_mutex_lock( &thread_flag_mutex );
	if ( colocation_thread ) {
		debug2( "Colocation scheduler thread already running, "
			"not starting another" );
		slurm_mutex_unlock( &thread_flag_mutex );
		return SLURM_ERROR;
	}

	/* since we do a join on this later we don't make it detached */
	slurm_thread_create(&colocation_thread, colocation_agent, NULL);

	slurm_mutex_unlock( &thread_flag_mutex );

	return SLURM_SUCCESS;
}

void fini(void)
{
	slurm_mutex_lock( &thread_flag_mutex );
	if ( colocation_thread ) {
		verbose( "Colocation scheduler plugin shutting down" );
		stop_colocation_agent();
		pthread_join(colocation_thread, NULL);
		colocation_thread = 0;
	}
	slurm_mutex_unlock( &thread_flag_mutex );
}

int slurm_sched_p_reconfig(void)
{
	colocation_reconfig();
	return SLURM_SUCCESS;
}

int _is_initial_colocation_candidate(struct job_record *job_ptr)
{
	uint64_t n_cpus = job_ptr->total_cpus ?
					   job_ptr->total_cpus :
					   job_ptr->details->min_cpus;
	uint64_t n_nodes = job_ptr->total_nodes ?
					   job_ptr->total_nodes :
					   job_ptr->details->min_nodes;
	if(n_nodes<2 /*&& job_ptr->details->share_res==1*/ && job_ptr->hwprofile!=NULL)
		return 1;
	return 0;
}

uint32_t slurm_sched_p_initial_priority(uint32_t last_prio,
					struct job_record *job_ptr)
{
    if(_is_initial_colocation_candidate(job_ptr))
		return 0; /* hold all new with 1 task */
    if(!job_ptr->select_jobinfo || !job_ptr->select_jobinfo->data)
        job_ptr->select_jobinfo = select_g_select_jobinfo_alloc();
    ((select_job_degradation_info*)(job_ptr->select_jobinfo->data))->text = xstrdup("lone job");
	return priority_g_set(last_prio, job_ptr);
}
