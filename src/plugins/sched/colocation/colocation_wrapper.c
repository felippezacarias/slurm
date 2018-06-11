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

uint32_t slurm_sched_p_initial_priority(uint32_t last_prio,
					struct job_record *job_ptr)
{
	return priority_g_set(last_prio, job_ptr);
}
