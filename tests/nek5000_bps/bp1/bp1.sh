#!/bin/bash

# Helper functions
function xyz()
{
  prod=$1
  split=$((prod/3))

  nex=$split
  nez=$split
  ney=$split

  if [[ $((prod%3)) -ne 0 ]]; then
    nez=$((split + 1))
  fi
  if [[ $((prod%3)) -eq 2 ]]; then
    ney=$((split + 1))
  fi

  nex=$((2**nex))
  ney=$((2**ney))
  nez=$((2**nez))

  echo "$nex $ney $nez"
}

function genbb()
{
  cp $1.box ttt.box
  $NEK5K_DIR/bin/genbox <<EOF
ttt.box
EOF
  $NEK5K_DIR/bin/genmap << EOF
box
.1
EOF
  $NEK5K_DIR/bin/reatore2 << EOF
box
$1
EOF
  rm ttt.box
  rm box.rea
  rm box.tmp
  mv box.map $1.map
}

function generate_boxes()
{
  cd $BP_ROOT/boxes
  # Run thorugh the box sizes
  for i in `seq $min_elem 1 $max_elem`
  do
    # Generate the boxes only if they have not
    # been generated before.
    if [[ ! -f b$i/b$i.rea ]]; then
      # Set the number of elements in box file.
      xyz=$(xyz $i)
      nex=$( echo $xyz | cut -f 1 -d ' ' )
      ney=$( echo $xyz | cut -f 2 -d ' ' )
      nez=$( echo $xyz | cut -f 3 -d ' ' )

      if [[ ! -e b$i ]]; then
         mkdir -p b$i
         sed "5s/.*/-$nex -$ney -$nez/" b.box > b$i/b$i.box
         cp b1e.rea b$i

         cd b$i
         genbb b$i &> log
         cd ..
      fi
    fi
  done
}


function configure_tests()
{
  export BP_ROOT="$root_dir"/tests/nek5000_bps

  # Settings for 1 node:

  # {min,max}_elem are the exponents for the number of elements, base is 2
  min_elem=1
  max_elem=18
  # the "order" here is actually number of 1D points, i.e. p+1, not p
  min_order=2
  max_order=9
  # the number of points is computed as num_elements*(p+1)**3
  max_points=3000000

  while (( 2**min_elem < num_proc_node )); do
     ((min_elem=min_elem+1))
  done

  # Settings for more than 1 node:

  local n=$num_nodes
  while (( n >= 2 )); do
     ((min_elem=min_elem+1))
     ((max_elem=max_elem+1))
     ((max_points=2*max_points))
     ((n=n/2))
  done
}

function set_max_elem_order()
{
  max_elem_order="$min_elem"
  local pp1="$1" s=
  for ((s = min_elem; s <= max_elem; s++)); do
    local npts=$(( 2**s * (pp1-1)**3 ))
    (( npts > max_points )) && break
    max_elem_order="$s"
  done
}

function build_tests()
{
  cd "$test_exe_dir"

  # Setup the sin version of the bp
  mkdir -p $1 
  cd $1 

  # Export variables needed by the 'makenek' script.
  local CFLAGS_orig="$CFLAGS" FFLAGS_orig="$FFLAGS"
  CFLAGS="${CFLAGS//:/\\:}"
  FFLAGS="${FFLAGS//:/\\:}"
  PPLIST="$NEK5K_EXTRA_PPLIST"
  # export NEK5K_DIR CFLAGS FFLAGS MPIF77 MPICC PPLIST
  export NEK5K_DIR MPIF77 MPICC PPLIST CFLAGS FFLAGS

  for i in `seq $min_order 1 $max_order`
  do
    # Only build nek5000 if it is not built
    # already.
    newbuild=false
    if [[ ! -e "lx$i" ]]; then
      mkdir -p lx$i
      newbuild=true
    fi

    set_max_elem_order "$i"

    local lelt=$(( (2**max_elem_order)/num_proc_run ))
    sed -e "s/lelt=[0-9]*/lelt=${lelt}/" \
        -e "s/lp=[0-9]*/lp=${num_proc_run}/" \
        $BP_ROOT/SIZE > SIZE

    cp -r $BP_ROOT/boxes/b?* $BP_ROOT/bp1/$1.usr lx$i/

    # Set lx1 in SIZE file
    sed "s/lx1=[0-9]*/lx1=${i}/" SIZE > lx$i/SIZE.new
    if [[ "$newbuild" == "true" ]]; then
      mv "lx$i"/SIZE.new "lx$i"/SIZE 
    elif [[ ! -f "lx$i"/SIZE ]]; then
      mv "lx$i"/SIZE.new "lx$i"/SIZE 
      newbuild=true
    else 
      if ! cmp -s "lx$i"/SIZE "lx$i"/SIZE.new; then
        newbuild=true
        rm "lx$i"/SIZE
        mv "lx$i"/SIZE.new "lx$i"/SIZE
      fi
    fi
    
    # Make the executable and copy it into all the
    # box directories
    cd lx$i
    echo "Building the $1 tests in directory $PWD ..."
    if [[ "$newbuild" = false ]]; then
      echo "Reusing the existing build ..."
    fi
    $BP_ROOT/makenek $1 &> buildlog
    if [[ ! -e nek5000 ]]; then
      echo "Error building the test, see 'buildlog' for details. Stop."
      CFLAGS="${CFLAGS_orig}"
      FFLAGS="${FFLAGS_orig}"
      return 1
    fi

    for j in `seq $min_elem 1 $max_elem_order`
    do
      cp ./nek5000 b$j/
    done

    cd .. ## lx$i
  done

  cd ..

  if [[ "$2" != "" ]]; then
    cp -r $1 $2
    cd $2

    for i in `seq $min_order 1 $max_order`
    do
      cp -r $BP_ROOT/bp1/$2.usr lx$i/
      cd lx$i
      rm nek5000 $1.usr > /dev/null 2>&1

      echo "Building the $2 tests in directory $PWD ..."
      $BP_ROOT/makenek $2 &> buildlog
      if [[ ! -e nek5000 ]]; then
        echo "Error building the test, see 'buildlog' for details. Stop."
        CFLAGS="${CFLAGS_orig}"
        FFLAGS="${FFLAGS_orig}"
        return 1
      fi

      set_max_elem_order "$i"

      for j in `seq $min_elem 1 $max_elem_order`
      do
        cp ./nek5000 b$j/
      done

      cd ..
    done
  fi

  CFLAGS="${CFLAGS_orig}"
  FFLAGS="${FFLAGS_orig}"
}

function nekmpi()
{
  cp $BP_ROOT/"submit.sh" .

  # This next check is important only for machines without virtual memory like
  # Blue Gene/Q.
  local size_out=($(size ./nek5000))
  local proc_size=${size_out[9]}
  if [[ -n "$node_virt_mem_lim" ]] && \
     (( proc_size * num_proc_node > node_virt_mem_lim * 1024*1024*1024 )); then
     echo "The total section size in ./nek5000 is too big: $proc_size ..."
     echo " ... in directory: $PWD"
     return 1
  fi

  ./submit.sh $1 $2
}

function run_tests()
{
  cd "$test_exe_dir"

  set_mpi_options
  local mpi_run="${MPIEXEC:-mpirun} $MPIEXEC_OPTS"
  export mpi_run="$mpi_run ${MPIEXEC_NP:--np} $num_proc_run $bind_sh"

  cd $1

  for i in `seq $min_order 1 $max_order`
  do
    cd lx$i
    set_max_elem_order "$i"
    for j in `seq $min_elem 1 $max_elem_order`
    do
      local npts=$(( ((i-1)**3) * (2**j) ))
      echo
      printf "Running order $((i-1)), with number of elements 2^$j;"
      echo " number of points is $npts."

      cd b$j
      $dry_run nekmpi b$j $num_proc_run
      cd ..
    done

    cd ..
  done

  cd ..
}

function build_and_run_tests()
{
  echo 'Setting up the tests ...'
  configure_tests
  echo "Generating the box meshes ..."
  $dry_run generate_boxes
  echo 'Buiding the sin and w tests ...'
  $dry_run build_tests zsin || return 1
#  $dry_run build_tests zsin zw || return 1
  echo 'Running the sin tests ...'
  run_tests zsin
  # W tests are commented as there is no diskquota
  # in vulcan to run both the tests
#  echo 'Running the w tests ...'
#  $dry_run run_tests zw
}

test_required_packages="nek5000"