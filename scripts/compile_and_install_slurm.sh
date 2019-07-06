#!/bin/bash 
##BSUB -W 36:00
#BSUB -W 1:00
#BSUB -J slurm
#BSUB -cwd .
#BSUB -o ./batch_slurm_%J.out
#BSUB -e ./batch_slurm_%J.err
#BSUB -n 16
#BSUB -q debug

slurm_source_dir="/gpfs/scratch/bsc28/bsc28161/colocation/slurm"

#####################################################################
#mn4
module purge
module load intel gcc/7.2.0 impi/2018.0 mkl/2018.0 python/2.7.13
install_dir="/gpfs/scratch/bsc28/bsc28161/colocation/install"
#####################################################################
#nord3
#module purge
#install_dir="/gpfs/scratch/bsc28/bsc28161/colocation/install_nord3"

#Python2.7
#module load gcc/6.2.0 intel impi/2017.0.098 MKL/2017.0.098 PYTHON/2.7.12_ML
#export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/apps/PYTHON/2.7.12_ML/lib
#export LDFLAGS="-L/apps/PYTHON/2.7.12_ML/lib"

#Python3
#module load intel impi/2017.0.098 MKL/2017.0.098 gcc/6.2.0 PYTHON/3.5.2_ML
#export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/apps/PYTHON/3.5.2_ML/lib
#export LDFLAGS="-L/apps/PYTHON/3.5.2_ML/lib -Wl,-rpath=/apps/.GLIBC/2.24/lib/"
##export LDFLAGS="-L/apps/PYTHON/3.5.2_ML/lib -Wl,-rpath=/apps/.GLIBC/2.24/lib/"
#####################################################################

echo "Compiling and installing Slurm from ${slurm_source_dir} to "\
"${install_dir}"

mkdir -p "${install_dir}"
mkdir -p "${install_dir}/slurm_varios/var/state"
mkdir -p "${install_dir}/slurm_varios/var/spool"
mkdir -p "${install_dir}/slurm_varios/log"

export LIBS="-lrt"
export CFLAGS=""

cd "${slurm_source_dir}"

echo "Running Configure"

./configure --exec-prefix=$install_dir/slurm_programs \
--bindir=$install_dir/slurm_programs/bin \
--sbindir=$install_dir/slurm_programs/sbin \
--datadir=$install_dir/slurm_varios/share \
--includedir=$install_dir/slurm_varios/include \
--libdir=$install_dir/slurm_varios/lib \
--libexecdir=$install_dir/slurm_varios/libexec \
--localstatedir=$install_dir/slurm_varios \
--sharedstatedir=$install_dir/slurm_varios \
--mandir=$install_dir/slurm_varios/man \
--prefix=$install_dir/slurm_programs --sysconfdir=$install_dir/slurm_conf 

echo "Compiling"
make clean
make -j10

echo "Installing"
make -j install

cd ..
cp -r slurm_conf $install_dir

openssl genrsa -out $install_dir/slurm_conf/slurm.key 1024
openssl rsa -in $install_dir/slurm_conf/slurm.key -pubout -out $install_dir/slurm_conf/slurm.cert

