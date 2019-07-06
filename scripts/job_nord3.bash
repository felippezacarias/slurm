#!/bin/bash 
##BSUB -W 36:00
#BSUB -W 1:00
#BSUB -J slurm
#BSUB -cwd .
#BSUB -o ./batch_slurm_%J.out
#BSUB -e ./batch_slurm_%J.err
#BSUB -n 32
#BSUB -q debug
##BSUB -n 4
##BSUB -q interactive

date

#https://www.ibm.com/support/knowledgecenter/en/SSETD4_9.1.3/lsf_config_ref/lsf_envars_job_exec.html
#https://hpc.llnl.gov/banks-jobs/running-jobs/slurm-srun-versus-ibm-csm-jsrun

module purge
module load gcc/6.2.0 intel impi/2017.0.098 MKL/2017.0.098 PYTHON/2.7.12_ML 
#module load intel impi/2017.0.098 MKL/2017.0.098 gcc/6.2.0 PYTHON/3.5.2_ML
install_dir="/gpfs/scratch/bsc28/bsc28161/colocation/install_nord3"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/apps/PYTHON/2.7.12_ML/lib
#export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/apps/PYTHON/3.5.2_ML/lib
export LDFLAGS="-L/apps/PYTHON/2.7.12_ML/lib"
#export LDFLAGS="-L/apps/PYTHON/3.5.2_ML/lib"


export PYTHONPATH=$PYTHONPATH:/gpfs/scratch/bsc28/bsc28161/colocation/install_nord3/slurm_varios/lib/degradation_model

ROOT=/gpfs/scratch/bsc28/bsc28161/colocation
INSTALL=$ROOT/install_nord3
slurm_conf_template=${ROOT}/slurm_conf/slurm.conf.template

slurmctld_port=$((($LSB_JOBID%65535))) 
slurmd_port=$(($slurmctld_port+12))
HOST=`hostname`

#Necessary to parse the host_list. the LSB_HOSTS gives the hostname for each processor
NODE_LIST=$(echo $LSB_HOSTS | sed "s/ /\n/g" | uniq | cut -d';' -f1 | paste -sd',')
echo $NODE_LIST


sed -e "s:TOKEN_HEAD_MASTER:$HOST:g" \
    -e "s:TOKEN_NUM_CTL_PORT:$slurmctld_port:g" \
    -e "s:TOKEN_NUM_D_PORT:$slurmd_port:g" \
    -e "s:TOKEN_NAME_NODES:$NODE_LIST:g" \
    $slurm_conf_template > $INSTALL/slurm_conf/slurm.conf

sed -i 's/install/install_nord3/g' $INSTALL/slurm_conf/slurm.conf

#optional
cp $ROOT/counters/* /tmp/
rm /tmp/SLURM_PYTHON_*
#rm /tmp/SLURM_PYTHON_GREEDY_ERROR.txt
rm $INSTALL/slurm_varios/var/state/* -r
rm $INSTALL/slurm_varios/var/spool/* -r

#very importante, otherwise we get:
#Filename: <type 'exceptions.ImportError'>
#Filename: /apps/PYTHON/2.7.13/INTEL/lib/python2.7/site-packages/scipy/_lib/_ccallback_c.so: undefined symbol: PyExc_SystemError
#export LD_PRELOAD=/apps/PYTHON/2.7.12_ML/lib/libpython2.7.so.1.0


LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/apps/.GLIBC/2.24/lib:/usr/lib64 /apps/.GLIBC/2.24/lib/ld-linux-x86-64.so.2 $INSTALL/slurm_programs/sbin/slurmctld

blaunch -z "$(echo $NODE_LIST | sed "s/,/ /g")" run_slurmd.bash $INSTALL/slurm_programs/sbin/slurmd 7000 &

sleep 5

#The index of the first job is 2

#here the colocation is [2,3],4,5, however using 2 nodes the execution is 2,3,4,5
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 20
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash 40
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 35
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 20

#here the colocation is [2,3],[4,5], however using 2 nodes it is executing [2,4],[3,5]
#I believe this happens because the slurm might use a least loaded algorithm to assign node to jobs.
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 20
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 40
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 35
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 20
#
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 45
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 45

#here the colocation is [2,3],4,[5,6],7, however using 2 nodes the execution is 2,3 (separate nodes),
#when 2 finalizes 4 executes in the free node, then 5 that is shared executes with 3. Job 3 and 4 will
#finish togheter but in this scenario job 6 executes with 5 and job 7 executes on the node left
$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 80 
$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 90
$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 45
$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 85
$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 40
$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 45
sleep 82
$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 60
$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 60
$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 40
$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 50

##$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 45

#real applications teste
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash "executables/bt-mz.A.1"
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash "executables/bt-mz.B.1"
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash "executables/stream"
#$INSTALL/slurm_programs/bin/sbatch --hwprofile=/tmp/black.csv job_install_test_colocation.bash "executables/bt-mz.B.1"

#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 170
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 180
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 175
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 160
#sleep 60
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 145



#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 80
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 85
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 130
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 65
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/barnes.csv job_install_test_colocation.bash 55
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 50
#sleep 30
#$INSTALL/slurm_programs/bin/sbatch -N 1 --hwprofile=/tmp/black.csv job_install_test_colocation.bash 50


sleep 7200
date
