import os
import re
import datetime
import pymysql
import pandas as pd


class MysqlManager():
    def __init__(self, host, port, user, password, data_base_name):
        super(MysqlManager, self).__init__()
        self.host = host
        self.port = port
        self.user = user
        self.password = password
        self.data_base_name = data_base_name
        self.db = None
        self.cursor = None
        # self.connect()

    def connect(self):
        self.db = pymysql.connect(host=self.host, port=self.port, user=self.user, password=self.password,
                                  db=self.data_base_name, charset='utf8')
        self.cursor = self.db.cursor()

    def insert_trade_delay_data(self, data):
        if len(data) <= 0:
            return
        
        my_db = pymysql.connect(host=self.host, port=self.port, user=self.user, password=self.password,
                                db=self.data_base_name, charset='utf8')
        cursor = my_db.cursor()
        for d in data:
            exchange_id = d[0]
            avg_new_order_system_delay = d[1]
            avg_cancel_order_system_delay = d[2]
            avg_new_order_exchange_delay = d[3]
            avg_cancel_order_exchange_delay = d[4]
            record_time = d[5]
            server_name = d[6]

            sql = f"insert into t_trade_delay(exchange_id,avg_new_order_system_delay,avg_cancel_order_system_delay,avg_new_order_exchange_delay,avg_cancel_order_exchange_delay,server_name,record_time) values('{exchange_id}',{avg_new_order_system_delay},{avg_cancel_order_system_delay},{avg_new_order_exchange_delay},{avg_cancel_order_exchange_delay},'{server_name}','{record_time}') on duplicate key update avg_new_order_system_delay={avg_new_order_system_delay},avg_cancel_order_system_delay={avg_cancel_order_system_delay},avg_new_order_exchange_delay={avg_new_order_exchange_delay},avg_cancel_order_exchange_delay={avg_cancel_order_exchange_delay}"
            cursor.execute(sql)
        my_db.commit()
        cursor.close()
        my_db.close()

    def create_trade_delay_table(self):
        my_db = pymysql.connect(host=self.host, port=self.port, user=self.user, password=self.password,
                                db=self.data_base_name, charset='utf8')
        cursor = my_db.cursor()
        sql = f"create table t_trade_delay(pk_id bigint unsigned primary key auto_increment, exchange_id varchar(128), avg_new_order_system_delay decimal(30,15), avg_cancel_order_system_delay decimal(30,15), avg_new_order_exchange_delay decimal(30,15), avg_cancel_order_exchange_delay decimal(30,15), server_name varchar(64), record_time datetime, gmt_create datetime default current_timestamp, gmt_modified datetime default current_timestamp on update current_timestamp)"
        cursor.execute(sql)
        sql_unique_key = f"alter table t_trade_delay add unique key exchange_id_server_time(exchange_id,server_name,record_time)"
        cursor.execute(sql_unique_key)
        results = cursor.fetchall()
        cursor.close()
        my_db.close()

if __name__ == '__main__':
    sql_manager = MysqlManager('172.31.36.194', 3306, 'root', 'Tr8EWXcvSjVwDeW%', 'rad_bp_se')
    sql_manager.create_trade_delay_table()