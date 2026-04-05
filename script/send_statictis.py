import requests
import sys
import os
from datetime import datetime, date, timedelta


def send_file(file_path, file_name):
    # 接口路径
    url = "http://52.69.12.140:8000/send_file"
    # 读文件内容
    with open(file_path, 'rb') as f:
        file_data = f.read()
    # 设置文件
    files = {
        'file': file_data
    }
    # 设置参数
    data = {
        "chat_id": "oc_dc1ab34780987dc0c07009946449feea",
        "file_type": "stream",
        "file_name": file_name
    }
    # post发送
    response = requests.post(url, files=files, data=data)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Parameter error!')
        sys.exit(0)

    run_res_path = sys.argv[1]
    last_date = date.today() + timedelta(days=-1)
    last_date_str = f'{last_date}'

    for file_name in os.listdir(run_res_path):
        if 'Statictis' in file_name and last_date_str in file_name:
            send_file(f'{run_res_path}/{file_name}', file_name)
