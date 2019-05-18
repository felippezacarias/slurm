#!/bin/bash

module load intel gcc/7.2.0 impi/2018.0 mkl/2018.0 python/2.7.13

##################################################################
## You can change the install folder modifying the variables below.
##################################################################

LOCAL=`pwd`
slurm_source_dir="${LOCAL}/slurm"
install_dir="${LOCAL}/install"


##################################################################



echo "Compiling and installing Slurm from ${slurm_source_dir} to "\
"${install_dir}"

mkdir -p "${install_dir}"
mkdir -p "${install_dir}/slurm_varios/var/state"
mkdir -p "${install_dir}/slurm_varios/var/spool"
mkdir -p "${install_dir}/slurm_varios/log"

export LIBS=-lrt
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
make -j10

echo "Installing"
make -j install

cd ..

##################################################################
## The slurm_conf folder must be inside the $install_dir as 
## specified during the configure step.
##################################################################

cp -r slurm_conf $install_dir


##################################################################
## This step is for the slurm autentication. 
##################################################################

openssl genrsa -out $install_dir/slurm_conf/slurm.key 1024
openssl rsa -in $install_dir/slurm_conf/slurm.key -pubout -out $install_dir/slurm_conf/slurm.cert

