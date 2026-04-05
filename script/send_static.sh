#!/bin/bash
source_path=/home/ec2-user/utrade-dev
dest_path=/home/ec2-user/utrade-dev/run_prd/files

script_path=/home/ec2-user/utrade-dev/script
cd $script_path

/opt/anaconda3/bin/python3.8 copy_file.py $source_path $dest_path

run_path=/home/ec2-user/utrade-dev/run_prd/files/statistic
run_path_list="/home/ec2-user/utrade-dev/run_prd/files/statistic /home/ec2-user/utrade-dev/run_prd/files"


echo $run_path
echo $run_path_list

/opt/anaconda3/bin/python3.8 merge.py $run_path_list
/opt/anaconda3/bin/python3.8 Run_Quick.py $run_path
/opt/anaconda3/bin/python3.8 send_statictis.py $run_path
