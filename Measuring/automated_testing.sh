#!/bin/bash

WAIT_TIME=10
ITERS=10

REFERENCE="reference"
RESTIR="restir"
RADIANCE="radiance_cascades"
CURRENT=""
MEAN_STDEV_SCRIPT="calc_mean_stdev.py"
GRAPH_SCRIPT="gen_graph.py"

source .venv/bin/activate

rm -f $REFERENCE*
for (( i=0; i<$ITERS; i++ )); do
  cd ..

  "./target/release/$REFERENCE" &> Measuring/$REFERENCE.csv &
  PID=$!

  sleep $WAIT_TIME

  kill $PID

  sleep 1

  cd Measuring

  python $MEAN_STDEV_SCRIPT $REFERENCE
done

python $GRAPH_SCRIPT $REFERENCE

# rm -f $RADIANCE*
# for (( i=0; i<$ITERS; i++ )); do
#   cd ..

#   "./target/release/$RADIANCE > Measuring/radiance_cascades.csv" &
#   PID=$!

#   sleep $WAIT_TIME

#   kill $PID

#   sleep 1

#   cd Measuring

#   python $MEAN_STDEV_SCRIPT $RADIANCE
# done

# python $GRAPH_SCRIPT $RADIANCE
