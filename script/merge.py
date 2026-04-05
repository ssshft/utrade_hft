import pandas as pd
import numpy as np
from datetime import datetime, date, timedelta
import time
import sys
import os


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print('Parameter error!')
        sys.exit(0)
    run_res_path = sys.argv[1]
    run_path_list = []
    for i in range(0, len(sys.argv)):
        if i >= 2:
            run_path_list.append(sys.argv[i])
    print(run_res_path)
    print(run_path_list)
    #run_res_path = './run'
    #run_path_list = ['./run1', './run2', './run3']
    #run_res_path = './run_bybit'
    #run_path_list = ['./run1_bybit', './run2_bybit']
    last_date = date.today() + timedelta(days=-1)
    date_list = [f'{last_date}']
    #date_list = ['2023-07-03', '2023-07-04', '2023-07-05', '2023-07-06', '2023-07-07', '2023-07-08', '2023-07-09']
    #start_time = '2023-06-29 03:40:00'
    start_time = None
    for date_s in date_list:
        run1_path = run_path_list[0]
        trading_type_order_dict = {}
        for f in os.listdir(run1_path):
            name_list = f.split('_')
            date_str = name_list[0]
            if date_s != date_str:
                continue
            print(f)
            name_str = name_list[1]
            if 'pairOrder' in name_str:
                all_pair = []
                for run_path in run_path_list:
                    run_f = f'{run_path}/{date_str}_{name_str}'
                    if os.path.exists(run_f):
                        df_pair = pd.read_csv(run_f, converters={'pairId': str})
                        all_pair.append(df_pair)
                if len(all_pair) > 0:
                    df_pair = pd.concat(all_pair)
                    df_pair = df_pair.sort_values(by='updateTime', ascending=True)
                    start_time_us = None
                    if start_time:
                        start_time_date = datetime.strptime(start_time, '%Y-%m-%d %H:%M:%S')
                        start_time_us = int(time.mktime(start_time_date.timetuple()) * 1000 * 1000)
                    if start_time_us:
                        df_pair = df_pair[df_pair['updateTime'] >= start_time_us]
                    df_pair.set_index('pairId', inplace=True)
                    df_pair.to_csv(f'{run_res_path}/{date_str}_{name_str}')

                    pair_group = df_pair.groupby('strategyName')
                    for name,group in pair_group:
                        group_path = f'{run_res_path}/{date_str}_{name}_{name_str}'
                        group.to_csv(group_path)

                    order_group = df_pair.groupby('tradingTypeOrder')
                    for name,group in order_group:
                        trading_type_order_dict[name] = group
                        group_path = f'{run_res_path}/{date_str}_{name}_{name_str}'
                        group.to_csv(group_path)

        for f in os.listdir(run1_path):
            name_list = f.split('_')
            date_str = name_list[0]
            if date_s != date_str:
                continue
            print(f)
            name_str = name_list[1]
            if 'quantOrder' in name_str:
                old_str = 'Due to the order could not be executed as maker,'
                new_str = 'Due to the order could not be executed as maker'
                old_str2 = 'invalid request,'
                new_str2 = 'invalid request '
                old_str3 = '],server_timestamp['
                new_str3 = ']server_timestamp['
                all_quant = []
                for run_path in run_path_list:
                    run_f = f'{run_path}/{date_str}_{name_str}'
                    if os.path.exists(run_f):
                        file_data = ""
                        with open(run_f, 'r') as f:
                            for line in f:
                                if old_str in line:
                                    line = line.replace(old_str, new_str)
                                    line = line.replace(old_str2, new_str2)
                                    line = line.replace(old_str3, new_str3)
                                file_data += line
                        with open(run_f, 'w') as f:
                            f.write(file_data)
                        df_quant = pd.read_csv(run_f, converters={'pairId': str})
                        all_quant.append(df_quant)
                if len(all_quant) > 0:
                    df_quant = pd.concat(all_quant)
                    df_quant = df_quant.sort_values(by='updateTime', ascending=True)
                    start_time_us = None
                    if start_time:
                        start_time_date = datetime.strptime(start_time, '%Y-%m-%d %H:%M:%S')
                        start_time_us = int(time.mktime(start_time_date.timetuple()) * 1000 * 1000)
                    if start_time_us:
                        df_quant = df_quant[df_quant['updateTime'] >= start_time_us]
                    df_quant.set_index('strategyName', inplace=True)
                    df_quant.to_csv(f'{run_res_path}/{date_str}_{name_str}')
                    quant_group = df_quant.groupby('strategyName')
                    for name,group in quant_group:
                        group_path = f'{run_res_path}/{date_str}_{name}_{name_str}'
                        group.to_csv(group_path)

                    for name,value in trading_type_order_dict.items():
                        pair_id_values = value.index
                        quant_order_trading_type_order = df_quant[df_quant['pairId'].isin(pair_id_values)]
                        group_path = f'{run_res_path}/{date_str}_{name}_{name_str}'
                        quant_order_trading_type_order.to_csv(group_path)
