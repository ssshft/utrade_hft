from Object import QuantOrder,smc
import pandas as pd
import numpy as np


class QuantOrderStatictis:
    def __init__(self):
        self.quant_order_dict = {}
        self.total_trade_amount = {}
        self.total_order_amount = {}
        self.total_trade_order_count = {}
        self.total_order_count = {}
        self.total_active_long_trade_amount = {}
        self.total_active_long_trade_price = {}
        self.total_active_long_trade_volume = {}
        self.total_active_short_trade_amount = {}
        self.total_active_short_trade_price = {}
        self.total_active_short_trade_volume = {}

        self.total_passive_long_trade_amount = {}
        self.total_passive_long_trade_price = {}
        self.total_passive_long_trade_volume = {}
        self.total_passive_short_trade_amount = {}
        self.total_passive_short_trade_price = {}
        self.total_passive_short_trade_volume = {}

        self.total_active_slippage = {}
        self.total_active_volume = {}
        self.total_passive_slippage = {}
        self.total_passive_volume = {}

        self.avg_active_new_order_system_delay = {}
        self.avg_active_cancel_order_system_delay = {}
        self.avg_passive_new_order_system_delay = {}
        self.avg_passive_cancel_order_system_delay = {}

    def load_file(self, file_path):
        df = pd.read_csv(file_path, converters={'pairId': str})
        norepeat_df = df.drop_duplicates(subset=['strategyOrderId'], keep='last')

        for index, row in norepeat_df.iterrows():
            quant_order = QuantOrder()
            quant_order.strategy_name = row['strategyName']
            quant_order.strategy_order_id = row['strategyOrderId']
            quant_order.system_order_id = row['systemOrderId']
            quant_order.exchange_order_id = row['exchangeOrderId']
            quant_order.instrument_key = row['instrumentKey']
            quant_order.order_type = row['orderType']
            quant_order.direction = row['direction']
            quant_order.order_status = row['orderStatus']
            quant_order.order_time_status = str(row['orderTimeStatus']).split('|')
            quant_order.target_price = row['targetPrice']
            quant_order.price = row['price']
            quant_order.volume = row['volume']
            quant_order.total_price_on_order = row['totalPriceOnOrder']
            quant_order.total_volume_on_order = row['totalVolumeOnOrder']
            quant_order.trade_volume = row['tradeVolume']
            quant_order.active_bid_price_1 = row['activeBidPrice1']
            quant_order.active_bid_volume_1 = row['activeBidVolume1']
            quant_order.active_ask_price_1 = row['activeAskPrice1']
            quant_order.active_ask_volume_1 = row['activeAskVolume1']
            quant_order.passive_bid_price_1 = row['passiveBidPrice1']
            quant_order.passive_bid_volume_1 = row['passiveBidVolume1']
            quant_order.passive_ask_price_1 = row['passiveAsk1Price1']
            quant_order.passive_ask_volume_1 = row['passiveAskVolume1']
            quant_order.total_short_fee = row['totalShortFee']
            quant_order.total_long_fee = row['totalLongFee']
            quant_order.order_time = row['orderTime']
            quant_order.update_time = row['updateTime']
            quant_order.kill_time = row['killTime']
            quant_order.error_id = row['errorId']
            quant_order.origin_error_msg = row['originErrorMsg']
            quant_order.reduce_only = bool(row['reduceOnly'])
            quant_order.pair_id = row['pairId']
            quant_order.algo_pair_id = row['algoPairId']
            quant_order.is_active_order = bool(row['isActiveOrder'])
            quant_order.rebalance = bool(row['rebalance'])
            quant_order.init()

            self.quant_order_dict[quant_order.strategy_order_id] = quant_order

    def get_all_quant_order_by_pair_id(self, pair_id):
        all_quant_orders = {}
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            if quant_order.pair_id == pair_id:
                all_quant_orders[strategy_order_id] = quant_order
        return all_quant_orders

    def calc_total_trade_amount(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_total_trade_amount = quant_order.get_total_trade_amount()
            if quant_order.instrument_key not in self.total_trade_amount:
                self.total_trade_amount[quant_order.instrument_key] = quant_order_total_trade_amount
            else:
                self.total_trade_amount[quant_order.instrument_key] = self.total_trade_amount[quant_order.instrument_key] + quant_order_total_trade_amount

    def calc_total_order_amount(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_total_order_amount = quant_order.get_total_order_amount()
            if quant_order.instrument_key not in self.total_order_amount:
                self.total_order_amount[quant_order.instrument_key] = quant_order_total_order_amount
            else:
                self.total_order_amount[quant_order.instrument_key] = self.total_order_amount[quant_order.instrument_key] + quant_order_total_order_amount
            
    def calc_trade_order_count(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_trade_order_count = quant_order.get_trade_order_count()
            if quant_order.instrument_key not in self.total_trade_order_count:
                self.total_trade_order_count[quant_order.instrument_key] = quant_order_trade_order_count
            else:
                self.total_trade_order_count[quant_order.instrument_key] = self.total_trade_order_count[quant_order.instrument_key] + quant_order_trade_order_count

    def calc_order_count(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_order_count = quant_order.get_order_count()
            if quant_order.instrument_key not in self.total_order_count:
                self.total_order_count[quant_order.instrument_key] = quant_order_order_count
            else:
                self.total_order_count[quant_order.instrument_key] = self.total_order_count[quant_order.instrument_key] + quant_order_order_count

    def calc_active_long_trade_amount(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_active_long_trade_amount = quant_order.get_active_long_trade_amount()
            if quant_order.instrument_key not in self.total_active_long_trade_amount:
                self.total_active_long_trade_amount[quant_order.instrument_key] = quant_order_active_long_trade_amount
            else:
                self.total_active_long_trade_amount[quant_order.instrument_key] = self.total_active_long_trade_amount[quant_order.instrument_key] + quant_order_active_long_trade_amount

    def calc_active_long_trade_price(self):
        for instrument in self.total_active_long_trade_volume:
            if instrument in self.total_active_long_trade_amount:
                if self.total_active_long_trade_volume[instrument] > 0:
                    price = self.total_active_long_trade_amount[instrument] / self.total_active_long_trade_volume[instrument]
                    self.total_active_long_trade_price[instrument] = price
                else:
                    self.total_active_long_trade_price[instrument] = 0

    def calc_active_long_trade_volume(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_active_long_trade_volume = quant_order.get_active_long_trade_volume()
            if quant_order.instrument_key not in self.total_active_long_trade_volume:
                self.total_active_long_trade_volume[quant_order.instrument_key] = quant_order_active_long_trade_volume
            else:
                self.total_active_long_trade_volume[quant_order.instrument_key] = self.total_active_long_trade_volume[quant_order.instrument_key] + quant_order_active_long_trade_volume

    def calc_active_short_trade_amount(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_active_short_trade_amount = quant_order.get_active_short_trade_amount()
            if quant_order.instrument_key not in self.total_active_short_trade_amount:
                self.total_active_short_trade_amount[quant_order.instrument_key] = quant_order_active_short_trade_amount
            else:
                self.total_active_short_trade_amount[quant_order.instrument_key] = self.total_active_short_trade_amount[quant_order.instrument_key] + quant_order_active_short_trade_amount

    def calc_active_short_trade_price(self):
        for instrument in self.total_active_short_trade_volume:
            if instrument in self.total_active_short_trade_amount:
                if self.total_active_short_trade_volume[instrument] > 0:
                    price = self.total_active_short_trade_amount[instrument] / self.total_active_short_trade_volume[instrument]
                    self.total_active_short_trade_price[instrument] = price
                else:
                    self.total_active_short_trade_price[instrument] = 0

    def calc_active_short_trade_volume(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_active_short_trade_volume = quant_order.get_active_short_trade_volume()
            if quant_order.instrument_key not in self.total_active_short_trade_volume:
                self.total_active_short_trade_volume[quant_order.instrument_key] = quant_order_active_short_trade_volume
            else:
                self.total_active_short_trade_volume[quant_order.instrument_key] = self.total_active_short_trade_volume[quant_order.instrument_key] + quant_order_active_short_trade_volume

    def calc_passive_long_trade_amount(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_passive_long_trade_amount = quant_order.get_passive_long_trade_amount()
            if quant_order.instrument_key not in self.total_passive_long_trade_amount:
                self.total_passive_long_trade_amount[quant_order.instrument_key] = quant_order_passive_long_trade_amount
            else:
                self.total_passive_long_trade_amount[quant_order.instrument_key] = self.total_passive_long_trade_amount[quant_order.instrument_key] + quant_order_passive_long_trade_amount

    def calc_passive_long_trade_price(self):
        for instrument in self.total_passive_long_trade_volume:
            if instrument in self.total_passive_long_trade_amount:
                if self.total_passive_long_trade_volume[instrument] > 0:
                    price = self.total_passive_long_trade_amount[instrument] / self.total_passive_long_trade_volume[instrument]
                    self.total_passive_long_trade_price[instrument] = price
                else:
                    self.total_passive_long_trade_price[instrument] = 0

    def calc_passive_long_trade_volume(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_passive_long_trade_volume = quant_order.get_passive_long_trade_volume()
            if quant_order.instrument_key not in self.total_passive_long_trade_volume:
                self.total_passive_long_trade_volume[quant_order.instrument_key] = quant_order_passive_long_trade_volume
            else:
                self.total_passive_long_trade_volume[quant_order.instrument_key] = self.total_passive_long_trade_volume[quant_order.instrument_key] + quant_order_passive_long_trade_volume


    def calc_passive_short_trade_amount(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_passive_short_trade_amount = quant_order.get_passive_short_trade_amount()
            if quant_order.instrument_key not in self.total_passive_short_trade_amount:
                self.total_passive_short_trade_amount[quant_order.instrument_key] = quant_order_passive_short_trade_amount
            else:
                self.total_passive_short_trade_amount[quant_order.instrument_key] = self.total_passive_short_trade_amount[quant_order.instrument_key] + quant_order_passive_short_trade_amount

    def calc_passive_short_trade_price(self):
        for instrument in self.total_passive_short_trade_volume:
            if instrument in self.total_passive_short_trade_amount:
                if self.total_passive_short_trade_volume[instrument] > 0:
                    price = self.total_passive_short_trade_amount[instrument] / self.total_passive_short_trade_volume[instrument]
                    self.total_passive_short_trade_price[instrument] = price
                else:
                    self.total_passive_short_trade_price[instrument] = 0

    def calc_passive_short_trade_volume(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            quant_order_passive_short_trade_volume = quant_order.get_passive_short_trade_volume()
            if quant_order.instrument_key not in self.total_passive_short_trade_volume:
                self.total_passive_short_trade_volume[quant_order.instrument_key] = quant_order_passive_short_trade_volume
            else:
                self.total_passive_short_trade_volume[quant_order.instrument_key] = self.total_passive_short_trade_volume[quant_order.instrument_key] + quant_order_passive_short_trade_volume

    def calc_active_slippage(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            active_slippage = quant_order.get_active_slippage()
            active_volume = quant_order.get_active_volume()
            if quant_order.instrument_key not in self.total_active_slippage:
                self.total_active_slippage[quant_order.instrument_key] = active_slippage
                self.total_active_volume[quant_order.instrument_key] = active_volume
            else:
                if (self.total_active_volume[quant_order.instrument_key] + active_volume) != 0:
                    self.total_active_slippage[quant_order.instrument_key] = (self.total_active_slippage[quant_order.instrument_key] * self.total_active_volume[quant_order.instrument_key] + active_slippage * active_volume) / (self.total_active_volume[quant_order.instrument_key] + active_volume)

    def calc_passive_slippage(self):
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            passive_slippage = quant_order.get_passive_slippage()
            passive_volume = quant_order.get_passive_volume()
            if quant_order.instrument_key not in self.total_passive_slippage:
                self.total_passive_slippage[quant_order.instrument_key] = passive_slippage
                self.total_passive_volume[quant_order.instrument_key] = passive_volume
            else:
                if (self.total_passive_volume[quant_order.instrument_key] + passive_volume) != 0:
                    self.total_passive_slippage[quant_order.instrument_key] = (self.total_passive_slippage[quant_order.instrument_key] * self.total_passive_volume[quant_order.instrument_key] + passive_slippage * passive_volume) / (self.total_passive_volume[quant_order.instrument_key] + passive_volume)

    def calc_active_new_order_system_delay(self):
        self.system_delay = {}
        self.order_count = {}
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            if quant_order.is_active_order is True:
                new_order_system_delay = quant_order.get_active_new_order_system_delay()
                if quant_order.instrument_key not in self.system_delay:
                    self.system_delay[quant_order.instrument_key] = new_order_system_delay
                    self.order_count[quant_order.instrument_key] = 1
                else:
                    self.system_delay[quant_order.instrument_key] = self.system_delay[quant_order.instrument_key] + new_order_system_delay
                    self.order_count[quant_order.instrument_key] = self.order_count[quant_order.instrument_key] + 1
        
        for instrument in self.system_delay:
            if self.order_count[instrument] > 0:
                self.avg_active_new_order_system_delay[instrument] = self.system_delay[instrument] / self.order_count[instrument]
            else:
                self.avg_active_new_order_system_delay[instrument] = 0

    def calc_active_cancel_order_system_delay(self):
        self.system_delay = {}
        self.order_count = {}
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            if quant_order.is_active_order is True:
                cancel_order_system_delay = quant_order.get_active_cancel_order_system_delay()
                if quant_order.instrument_key not in self.system_delay:
                    self.system_delay[quant_order.instrument_key] = cancel_order_system_delay
                    self.order_count[quant_order.instrument_key] = 1
                else:
                    self.system_delay[quant_order.instrument_key] = self.system_delay[quant_order.instrument_key] + cancel_order_system_delay
                    self.order_count[quant_order.instrument_key] = self.order_count[quant_order.instrument_key] + 1
        
        for instrument in self.system_delay:
            if self.order_count[instrument] > 0:
                self.avg_active_cancel_order_system_delay[instrument] = self.system_delay[instrument] / self.order_count[instrument]
            else:
                self.avg_active_cancel_order_system_delay[instrument] = 0

    def calc_passive_new_order_system_delay(self):
        self.system_delay = {}
        self.order_count = {}
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            if quant_order.is_active_order is not True:
                new_order_system_delay = quant_order.get_passive_new_order_system_delay()
                if quant_order.instrument_key not in self.system_delay:
                    self.system_delay[quant_order.instrument_key] = new_order_system_delay
                    self.order_count[quant_order.instrument_key] = 1
                else:
                    self.system_delay[quant_order.instrument_key] = self.system_delay[quant_order.instrument_key] + new_order_system_delay
                    self.order_count[quant_order.instrument_key] = self.order_count[quant_order.instrument_key] + 1
        
        for instrument in self.system_delay:
            if self.order_count[instrument] > 0:
                self.avg_passive_new_order_system_delay[instrument] = self.system_delay[instrument] / self.order_count[instrument]
            else:
                self.avg_passive_new_order_system_delay[instrument] = 0

    def calc_passive_cancel_order_system_delay(self):
        self.system_delay = {}
        self.order_count = {}
        for strategy_order_id in self.quant_order_dict:
            quant_order = self.quant_order_dict[strategy_order_id]
            if quant_order.is_active_order is not True:
                cancel_order_system_delay = quant_order.get_passive_cancel_order_system_delay()
                if quant_order.instrument_key not in self.system_delay:
                    self.system_delay[quant_order.instrument_key] = cancel_order_system_delay
                    self.order_count[quant_order.instrument_key] = 1
                else:
                    self.system_delay[quant_order.instrument_key] = self.system_delay[quant_order.instrument_key] + cancel_order_system_delay
                    self.order_count[quant_order.instrument_key] = self.order_count[quant_order.instrument_key] + 1
        
        for instrument in self.system_delay:
            if self.order_count[instrument] > 0:
                self.avg_passive_cancel_order_system_delay[instrument] = self.system_delay[instrument] / self.order_count[instrument]
            else:
                self.avg_passive_cancel_order_system_delay[instrument] = 0



