from datetime import datetime, date, timedelta
import sys
import shutil
import os

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print('Parameter error!')
        sys.exit(0)
    source_path = sys.argv[1]
    dest_path = sys.argv[2]
    last_date = date.today() + timedelta(days=-1)

    source_pair_file = f'{source_path}/run_prd/{last_date}_pairOrder.csv'
    dest_pair_file = f'{dest_path}/{last_date}_pairOrder.csv'
    shutil.copy(source_pair_file, dest_pair_file)

    source_quant_file = f'{source_path}/run_prd/{last_date}_quantOrder.csv'
    dest_quant_file = f'{dest_path}/{last_date}_quantOrder.csv'
    shutil.copy(source_quant_file, dest_quant_file)
