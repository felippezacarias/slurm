Slurm Workload Manager
--------------------------------------------------------

This is the Slurm Workload Manager. Slurm
is an open-source cluster resource management and job scheduling system
that strives to be simple, scalable, portable, fault-tolerant, and
interconnect agnostic. Slurm currently has been tested only under Linux.

As a cluster resource manager, Slurm provides three key functions. First,
it allocates exclusive and/or non-exclusive access to resources
(compute nodes) to users for some duration of time so they can perform
work. Second, it provides a framework for starting, executing, and
monitoring work (normally a parallel job) on the set of allocated
nodes. Finally, it arbitrates conflicting requests for resources by
managing a queue of pending work.

COLOCATION
----------

This version of slurm contains a developed scheduling plugin to perform
colocation of single node multithread applications. It works by calculating
the degradation between them and placing in the same node jobs that can 
execute together.

COMPILING AND INSTALLING THE DISTRIBUTION
-----------------------------------------

To compile use the scripts provided in **scripts/**.

ADDITIONAL INFORMATION
----------------------

**In order to use the colocation plugin, the below environment variables 
must be set:**

  PYTHONPATH        [ ]
     This environment variable must point to the degradation model folder.
     The folder contains the python script that will be used by the colocation
     plugin for compute the degradation between jobs. Ex:
     export PYTHONPATH=$PYTHONPATH:${install_folder}/slurm_varios/lib/degradation_model
  
  LD_PRELOAD        [ ]
     This environment variable must point to the python shared lib. This variable is
     very importante, otherwise during the execution of the python script from
     **degradation_model** folder there will be erros like:
     **<type 'exceptions.ImportError'>**
     **undefined symbol: PyExc_SystemError**
     Ex: export LD_PRELOAD=${python_system_install}/lib/libpython2.7.so.1.0


There are also some additional dependencies that must be installed before using
the colocation plugin:

  Python
  Numpy
  Scikit-learn

SOURCE DISTRIBUTION HIERARCHY
-----------------------------

The top-level distribution directory contains this README as well as
other high-level documentation files, and the scripts used to configure
and build Slurm (see INSTALL). Subdirectories contain the source-code
for Slurm as well as a DejaGNU test suite and further documentation. A
quick description of the subdirectories of the Slurm distribution follows:

  src/        [ Slurm source ]
     Slurm source code is further organized into self explanatory
     subdirectories such as src/api, src/slurmctld, etc.

  doc/        [ Slurm documentation ]
     The documentation directory contains some latex, html, and ascii
     text papers, READMEs, and guides. Manual pages for the Slurm
     commands and configuration files are also under the doc/ directory.

  etc/        [ Slurm configuration ]
     The etc/ directory contains a sample config file, as well as
     some scripts useful for running Slurm.

  slurm/      [ Slurm include files ]
     This directory contains installed include files, such as slurm.h
     and slurm_errno.h, needed for compiling against the Slurm API.

  testsuite/  [ Slurm test suite ]
     The testsuite directory contains the framework for a set of
     DejaGNU and "make check" type tests for Slurm components.
     There is also an extensive collection of Expect scripts.

  auxdir/     [ autotools directory ]
     Directory for autotools scripts and files used to configure and
     build Slurm

  contribs/   [ helpful tools outside of Slurm proper ]
     Directory for anything that is outside of slurm proper such as a
     different api or such.  To have this build you need to do a
     make contrib/install-contrib.
  
   scripts/        [ Colocation configuration ]
     The directory contains sample config files, as well as
     some scripts useful for seting up and running Slurm in a cluster
     as a job or locally.

LEGAL
-----

Slurm is provided "as is" and with no warranty. This software is
distributed under the GNU General Public License, please see the files
COPYING, DISCLAIMER, and LICENSE.OpenSSL for details.
