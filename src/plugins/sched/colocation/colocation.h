/*****************************************************************************\
 *  colocation.h - Header for contention awareness plugin 
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


#ifndef _SLURM_COLOCATION_H
#define _SLURM_COLOCATION_H
#include "src/plugins/select/cons_tres/select_cons_tres.h"

/* colocation_agent - detached thread periodically when pending jobs can start */
extern void *colocation_agent(void *args);

/* Terminate colocation_agent */
extern void stop_colocation_agent(void);

/* Note that slurm.conf has changed */
extern void colocation_reconfig(void);

#endif	/* _SLURM_COLOCATION_H */
