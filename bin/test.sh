#!/bin/bash

RW_SERVER="./ZotDonate_RWserver"
MT_SERVER="./ZotDonate_MTserver"
RCLIENT="./zotDonate_Rclient"
WCLIENT="./zotDonate_Wclient"
CLIENT="./zotDonate_client"
RW_SERVER_LOG="rw_server_log.txt"
MT_SERVER_LOG="mt_server_log.txt"
R_PORT=8080
W_PORT=8081
PORT=8090
SERVER_IP="127.0.0.1"

# Compile the server if necessary
make

# Function to run RWserver Scenario 1
run_rw_scenario1() {
  $RCLIENT $SERVER_IP $R_PORT << EOF
/cinfo 0
/logout
EOF

  $RCLIENT $SERVER_IP $R_PORT << EOF
/cinfo 2
/cinfo 3
/logout
EOF

  $RCLIENT $SERVER_IP $R_PORT << EOF
/cinfo 3
/logout
EOF
}

# Function to run RWserver Scenario 2
run_rw_scenario2() {
  $WCLIENT $SERVER_IP $W_PORT << EOF
/donate 4 30
/donate 0 20
/donate 1 2
/donate 2 3
EOF

  $RCLIENT $SERVER_IP $R_PORT << EOF
/stats
/logout
EOF

  $WCLIENT $SERVER_IP $W_PORT << EOF
/donate 3 60
EOF

  $RCLIENT $SERVER_IP $R_PORT << EOF
/stats
EOF

  $WCLIENT $SERVER_IP $W_PORT << EOF
/donate 2 50
/donate 1 15
EOF

  $RCLIENT $SERVER_IP $R_PORT << EOF
/stats
/logout
EOF

  $RCLIENT $SERVER_IP $R_PORT << EOF
/logout
EOF

  $WCLIENT $SERVER_IP $W_PORT << EOF
/logout
EOF
}

# Function to run MTserver Scenario
run_mt_scenario() {
  $CLIENT $SERVER_IP $PORT << EOF
/donate 0 50
/logout
EOF

  $CLIENT $SERVER_IP $PORT << EOF
/top
EOF

  $CLIENT $SERVER_IP $PORT << EOF
/donate 1 100
/logout
EOF

  $CLIENT $SERVER_IP $PORT << EOF
/top
EOF

  $CLIENT $SERVER_IP $PORT << EOF
/donate 2 150
/logout
EOF

  $CLIENT $SERVER_IP $PORT << EOF
/top
/logout
EOF
}

# Start the RWserver
$RW_SERVER $R_PORT $W_PORT $RW_SERVER_LOG &

# Give the RWserver some time to start
sleep 2

# Run the RWserver scenarios 100 times each
for i in {1..100}; do
  echo "Running RWserver Scenario 1, iteration $i"
  run_rw_scenario1
  echo "Running RWserver Scenario 2, iteration $i"
  run_rw_scenario2
done

# Kill the RWserver after testing
pkill -f "$RW_SERVER"

# Start the MTserver
$MT_SERVER $PORT $MT_SERVER_LOG &

# Give the MTserver some time to start
sleep 2

# Run the MTserver scenario 100 times
for i in {1..100}; do
  echo "Running MTserver Scenario, iteration $i"
  run_mt_scenario
done

# Kill the MTserver after testing
pkill -f "$MT_SERVER"

echo "Testing completed."
