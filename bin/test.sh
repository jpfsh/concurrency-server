#!/bin/bash

SERVER="./ZotDonate_RWserver"
RCLIENT="./zotDonate_Rclient"
WCLIENT="./zotDonate_Wclient"
SERVER_LOG="server_log.txt"
R_PORT=8080
W_PORT=8081
SERVER_IP="127.0.0.1"

run_scenario1() {
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

# Function to run Scenario 2
run_scenario2() {
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

# Start the server
$SERVER $R_PORT $W_PORT $SERVER_LOG &

# Give the server some time to start
sleep 2

# Run the scenarios 100 times each
for i in {1..100}; do
  echo "Running Scenario 1, iteration $i"
  run_scenario1
  echo "Running Scenario 2, iteration $i"
  run_scenario2
done

# Kill the server after testing
pkill -f "$SERVER"

echo "Testing completed."
