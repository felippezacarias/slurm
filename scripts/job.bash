#!/bin/bash 
#SBATCH --job-name=slurm
#SBATCH -D .
#SBATCH --output=batch-%j.out
#SBATCH -N 2
#SBATCH --time=01:59:59
#SBATCH --qos=debug

date

#module load gcc/7.2.0 intel impi/2018.0 mkl/2018.0 python/2.7.13
module load gcc/7.2.0 intel impi/2018.0 mkl/2018.0 python/2.7.13 openmpi/1.10.4


ROOT=//ROOT_FOLDER_OF_SLURM_INSTALL//
INSTALL=$ROOT/install
slurm_conf_template=${ROOT}/slurm_conf/slurm.conf.template

slurmctld_port=$((($SLURM_JOBID%65535))) 
slurmd_port=$(($slurmctld_port+12))
HOST=`hostname`

sed -e s:TOKEN_HEAD_MASTER:$HOST: \
    -e s:TOKEN_NUM_CTL_PORT:$slurmctld_port: \
    -e s:TOKEN_NUM_D_PORT:$slurmd_port: \
    -e s:TOKEN_NAME_NODES:$SLURM_JOB_NODELIST: \
    $slurm_conf_template > $INSTALL/slurm_conf/slurm.conf


#optional
cp $ROOT/counters/* /tmp/
rm /tmp/SLURM_PYTHON_*
#rm /tmp/SLURM_PYTHON_GREEDY_ERROR.txt
rm $INSTALL/slurm_varios/var/state/* -r

export PYTHONPATH=$PYTHONPATH:$INSTALL/slurm_varios/lib/degradation_model
export LD_PRELOAD=/usr/lib/libpython2.7.so.1.0

$INSTALL/slurm_programs/sbin/slurmctld
srun -w $SLURM_JOB_NODELIST ./run_slurmd.bash $INSTALL/slurm_programs/sbin/slurmd 7000 &
#this line below does not work because as soon as the slurmd issue finishes, the srun also finishes then the executable is killed. So it is not able to stay in background
#srun -w $SLURM_JOB_NODELIST $INSTALL/slurm_programs/sbin/slurmd  &

sleep 5

#The index of the first job is 2

#here the colocation is [2,3],4,5, however using 2 nodes the execution is 2,3,4,5
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 20
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 40
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 35
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 20

#here the colocation is [2,3],[4,5], however using 2 nodes it is executing [2,4],[3,5]
#I believe this happens because the slurm might use a least loaded algorithm to assign node to jobs.
$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 20
$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 40
$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 35
$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 20

#here the colocation is [2,3],4,[5,6],7, however using 2 nodes the execution is 2,3 (separate nodes),
#when 2 finalizes 4 executes in the free node, then 5 that is shared executes with 3. Job 3 and 4 will
#finish togheter but in this scenario job 6 executes with 5 and job 7 executes on the node left
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 20
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 40
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 20
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 35
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 20
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 25

#real applications teste
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash "executables/bt-mz.A.1"
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash "executables/bt-mz.B.1"
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash "executables/stream"
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash "executables/bt-mz.B.1"

sleep 7200
date
