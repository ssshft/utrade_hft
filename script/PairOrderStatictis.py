from Object import PairOrder
from QuantOrderStatictis import QuantOrderStatictis
import pandas as pd
import numpy as np
from datetime import datetime, date, timedelta
import time


class PairOrderStatictis:
    def __init__(self):
        self.quant_order_statictis = QuantOrderStatictis()
        self.pair_order_dict = {}
        self.total_active_trade_amount = {}
        self.total_passive_trade_amount = {}
        self.total_active_order_amount = {}
        self.total_passive_order_amount = {}
        self.total_active_trade_order_count = {}
        self.total_passive_trade_order_count = {}
        self.total_active_order_count = {}
        self.total_passive_order_count = {}

        self.total_active_trade_rate = {}
        self.total_passive_trade_rate = {}

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
        self.avg_active_new_order_exchange_delay = {}
        self.avg_passive_new_order_system_delay = {}
        self.avg_passive_cancel_order_system_delay = {}
        self.avg_passive_new_order_exchange_delay = {}

        self.total_open_long_target_spread = {}
        self.total_open_long_target_volume = {}
        self.total_open_short_target_spread = {}
        self.total_open_short_target_volume = {}
        self.total_close_long_target_spread = {}
        self.total_close_long_target_volume = {}
        self.total_close_short_target_spread = {}
        self.total_close_short_target_volume = {}

        self.total_open_long_real_spread = {}
        self.total_open_long_real_volume = {}
        self.total_open_short_real_spread = {}
        self.total_open_short_real_volume = {}
        self.total_close_long_real_spread = {}
        self.total_close_long_real_volume = {}
        self.total_close_short_real_spread = {}
        self.total_close_short_real_volume = {}

        self.close_long_open_long_real_spread = {}
        self.open_short_close_short_real_spread = {}

        self.active_long_real_spread = {}
        self.active_short_real_spread = {}
        self.active_long_real_spread_vol = {}
        self.active_short_real_spread_vol = {}

        self.active_long_real_spread_volume = {}
        self.active_short_real_spread_volume = {}
        self.passive_long_real_spread_volume = {}
        self.passive_short_real_spread_volume = {}
        self.active_long_real_spread_price = {}
        self.active_short_real_spread_price = {}
        self.passive_long_real_spread_price = {}
        self.passive_short_real_spread_price = {}

        self.avg_active_depth_delay = {}
        self.avg_passive_depth_delay = {}
        self.avg_min_active_passive_depth_delay = {}
        self.avg_active_generate_delay = {}

        self.pair_total_volume = {}
        self.pair_active_total_price = {}
        self.pair_passive_total_price = {}

    def load_file(self, pair_order_path, quant_order_path, start_time=None):
        start_time_us = None
        if start_time:
            start_time_date = datetime.strptime(start_time, '%Y-%m-%d %H:%M:%S')
            start_time_us = int(time.mktime(start_time_date.timetuple()) * 1000 * 1000)

        self.quant_order_statictis.load_file(quant_order_path)

        df = pd.read_csv(pair_order_path, converters={'pairId': str})
        norepeat_df = df.drop_duplicates(subset=['pairId'], keep='last')

        for index, row in norepeat_df.iterrows():
            pair_order = PairOrder()
            pair_order.pair_id = row['pairId']
            pair_order.algo_pair_id = row['algoPairId']
            pair_order.strategy_name = row['strategyName']
            pair_order.base_asset = row['baseAsset']
            pair_order.trading_type_order = row['tradingTypeOrder']
            pair_order.trading_type_offset = row['tradingTypeOffset']
            pair_order.target_volume = row['targetVolume']
            pair_order.active_instrument_key = row['activeInstrumentKey']
            pair_order.active_direction = row['activeDirection']
            pair_order.active_target_price = row['activeTargetPrice']
            pair_order.active_bid_price_1 = row['activeBidPrice1']
            pair_order.active_bid_volume_1 = row['activeBidVolume1']
            pair_order.active_ask_price_1 = row['activeAskPrice1']
            pair_order.active_ask_volume_1 = row['activeAskVolume1']
            pair_order.passive_instrument_key = row['passiveInstrumentKey']
            pair_order.passive_direction = row['passiveDirection']
            pair_order.passive_target_price = row['passiveTargetPrice']
            pair_order.passive_bid_price_1 = row['passiveBidPrice1']
            pair_order.passive_bid_volume_1 = row['passiveBidVolume1']
            pair_order.passive_ask_price_1 = row['passiveAsk1Price1']
            pair_order.passive_ask_volume_1 = row['passiveAskVolume1']
            pair_order.spread_bid_ask = row['spreadBidAsk']
            pair_order.spread_bid_bid = row['spreadBidBid']
            pair_order.spread_ask_bid = row['spreadAskBid']
            pair_order.spread_ask_ask = row['spreadAskAsk']
            pair_order.generate_ts = row['generateTs']
            pair_order.active_depth_ts = row['activeDepthTs']
            pair_order.passive_depth_ts = row['passiveDepthTs']
            pair_order.active_total_price_on_order = row['activeTotalPriceOnOrder']
            pair_order.active_total_volume_on_order = row['activeTotalVolumeOnOrder']
            pair_order.passive_total_price_on_order = row['passiveTotalPriceOnOrder']
            pair_order.passive_total_volume_on_order = row['passiveTotalVolumeOnOrder']
            pair_order.pair_total_volume = row['pairTotalVolume']
            pair_order.pair_active_total_price = row['pairActiveTotalPrice']
            pair_order.pair_passive_total_price = row['pairPassiveTotalPrice']
            pair_order.active_frozen_price = row['activeFrozenPrice']
            pair_order.active_frozen_volume = row['activeFrozenVolume']
            pair_order.passive_frozen_price = row['passiveFrozenPrice']
            pair_order.passive_frozen_volume = row['passiveFrozenVolume']
            pair_order.active_account_id = row['activeAccountId']
            pair_order.passive_account_id = row['passiveAccountId']
            pair_order.status = row['status']
            pair_order.rebalance_flag = bool(row['rebalanceFlag'])
            pair_order.create_time = row['createTime']
            pair_order.update_time = row['updateTime']

            if start_time_us:
                if pair_order.create_time < start_time_us:
                    continue

            self.pair_order_dict[pair_order.pair_id] = pair_order

            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
            if pair_order.active_total_volume_on_order > 0:
                self.pair_total_volume[pair_instrument_key] = pair_order.pair_total_volume
                self.pair_active_total_price[pair_instrument_key] = pair_order.pair_active_total_price
                self.pair_passive_total_price[pair_instrument_key] = pair_order.pair_passive_total_price

        norepeat_first_df = df.drop_duplicates(subset=['pairId'], keep='first')
        for index, row in norepeat_first_df.iterrows():
            pair_id = row['pairId']
            generate_ts = row['generateTs']
            active_depth_ts = row['activeDepthTs']
            passive_depth_ts = row['passiveDepthTs']
            create_time = row['createTime']

            if start_time_us:
                if create_time < start_time_us:
                    continue

            self.pair_order_dict[pair_id].create_time = create_time
            self.pair_order_dict[pair_id].generate_ts = generate_ts
            self.pair_order_dict[pair_id].active_depth_ts = active_depth_ts
            self.pair_order_dict[pair_id].passive_depth_ts = passive_depth_ts


    def calc_active_total_trade_amount(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    quant_order_total_trade_amount = quant_order.get_total_trade_amount()
                    if pair_instrument_key not in self.total_active_trade_amount:
                        self.total_active_trade_amount[pair_instrument_key] = quant_order_total_trade_amount
                    else:
                        self.total_active_trade_amount[pair_instrument_key] = self.total_active_trade_amount[pair_instrument_key] + quant_order_total_trade_amount

    def calc_passive_total_trade_amount(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    quant_order_total_trade_amount = quant_order.get_total_trade_amount()
                    if pair_instrument_key not in self.total_passive_trade_amount:
                        self.total_passive_trade_amount[pair_instrument_key] = quant_order_total_trade_amount
                    else:
                        self.total_passive_trade_amount[pair_instrument_key] = self.total_passive_trade_amount[pair_instrument_key] + quant_order_total_trade_amount

    def calc_active_total_order_amount(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    quant_order_total_order_amount = quant_order.get_total_order_amount()
                    if pair_instrument_key not in self.total_active_order_amount:
                        self.total_active_order_amount[pair_instrument_key] = quant_order_total_order_amount
                    else:
                        self.total_active_order_amount[pair_instrument_key] = self.total_active_order_amount[pair_instrument_key] + quant_order_total_order_amount

    def calc_passive_total_order_amount(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    quant_order_total_order_amount = quant_order.get_total_order_amount()
                    if pair_instrument_key not in self.total_passive_order_amount:
                        self.total_passive_order_amount[pair_instrument_key] = quant_order_total_order_amount
                    else:
                        self.total_passive_order_amount[pair_instrument_key] = self.total_passive_order_amount[pair_instrument_key] + quant_order_total_order_amount

    def calc_active_trade_order_count(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    quant_order_trade_order_count = quant_order.get_trade_order_count()
                    if pair_instrument_key not in self.total_active_trade_order_count:
                        self.total_active_trade_order_count[pair_instrument_key] = quant_order_trade_order_count
                    else:
                        self.total_active_trade_order_count[pair_instrument_key] = self.total_active_trade_order_count[pair_instrument_key] + quant_order_trade_order_count

    def calc_passive_trade_order_count(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    quant_order_trade_order_count = quant_order.get_trade_order_count()
                    if pair_instrument_key not in self.total_passive_trade_order_count:
                        self.total_passive_trade_order_count[pair_instrument_key] = quant_order_trade_order_count
                    else:
                        self.total_passive_trade_order_count[pair_instrument_key] = self.total_passive_trade_order_count[pair_instrument_key] + quant_order_trade_order_count

    def calc_active_order_count(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    quant_order_order_count = quant_order.get_order_count()
                    if pair_instrument_key not in self.total_active_order_count:
                        self.total_active_order_count[pair_instrument_key] = quant_order_order_count
                    else:
                        self.total_active_order_count[pair_instrument_key] = self.total_active_order_count[pair_instrument_key] + quant_order_order_count

    def calc_passive_order_count(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    quant_order_order_count = quant_order.get_order_count()
                    if pair_instrument_key not in self.total_passive_order_count:
                        self.total_passive_order_count[pair_instrument_key] = quant_order_order_count
                    else:
                        self.total_passive_order_count[pair_instrument_key] = self.total_passive_order_count[pair_instrument_key] + quant_order_order_count

    def calc_active_trade_rate(self):
        for pair_instrument_key in self.total_active_order_count:
            if self.total_active_order_count[pair_instrument_key] > 0:
                self.total_active_trade_rate[pair_instrument_key] = self.total_active_trade_order_count.get(pair_instrument_key, 0) / self.total_active_order_count[pair_instrument_key]

    def calc_passive_trade_rate(self):
        for pair_instrument_key in self.total_passive_order_count:
            if self.total_passive_order_count[pair_instrument_key] > 0:
                self.total_passive_trade_rate[pair_instrument_key] = self.total_passive_trade_order_count.get(pair_instrument_key, 0) / self.total_passive_order_count[pair_instrument_key]

    def calc_active_long_trade_amount(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    quant_order_active_long_trade_amount = quant_order.get_active_long_trade_amount()
                    if pair_instrument_key not in self.total_active_long_trade_amount:
                        self.total_active_long_trade_amount[pair_instrument_key] = quant_order_active_long_trade_amount
                    else:
                        self.total_active_long_trade_amount[pair_instrument_key] = self.total_active_long_trade_amount[pair_instrument_key] + quant_order_active_long_trade_amount

    def calc_active_long_trade_price(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
            if self.total_active_long_trade_volume.get(pair_instrument_key, 0) != 0:
                price = self.total_active_long_trade_amount[pair_instrument_key] / self.total_active_long_trade_volume[pair_instrument_key]
                self.total_active_long_trade_price[pair_instrument_key] = price
            else:
                self.total_active_long_trade_price[pair_instrument_key] = 0

    def calc_active_long_trade_volume(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    quant_order_active_long_trade_volume = quant_order.get_active_long_trade_volume()
                    if pair_instrument_key not in self.total_active_long_trade_volume:
                        self.total_active_long_trade_volume[pair_instrument_key] = quant_order_active_long_trade_volume
                    else:
                        self.total_active_long_trade_volume[pair_instrument_key] = self.total_active_long_trade_volume[pair_instrument_key] + quant_order_active_long_trade_volume

    def calc_active_short_trade_amount(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    quant_order_active_short_trade_amount = quant_order.get_active_short_trade_amount()
                    if pair_instrument_key not in self.total_active_short_trade_amount:
                        self.total_active_short_trade_amount[pair_instrument_key] = quant_order_active_short_trade_amount
                    else:
                        self.total_active_short_trade_amount[pair_instrument_key] = self.total_active_short_trade_amount[pair_instrument_key] + quant_order_active_short_trade_amount

    def calc_active_short_trade_price(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
            if self.total_active_short_trade_volume.get(pair_instrument_key, 0) != 0:
                price = self.total_active_short_trade_amount[pair_instrument_key] / self.total_active_short_trade_volume[pair_instrument_key]
                self.total_active_short_trade_price[pair_instrument_key] = price
            else:
                self.total_active_short_trade_price[pair_instrument_key] = 0

    def calc_active_short_trade_volume(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    quant_order_active_short_trade_volume = quant_order.get_active_short_trade_volume()
                    if pair_instrument_key not in self.total_active_short_trade_volume:
                        self.total_active_short_trade_volume[pair_instrument_key] = quant_order_active_short_trade_volume
                    else:
                        self.total_active_short_trade_volume[pair_instrument_key] = self.total_active_short_trade_volume[pair_instrument_key] + quant_order_active_short_trade_volume

    def calc_passive_long_trade_amount(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    quant_order_passive_long_trade_amount = quant_order.get_passive_long_trade_amount()
                    if pair_instrument_key not in self.total_passive_long_trade_amount:
                        self.total_passive_long_trade_amount[pair_instrument_key] = quant_order_passive_long_trade_amount
                    else:
                        self.total_passive_long_trade_amount[pair_instrument_key] = self.total_passive_long_trade_amount[pair_instrument_key] + quant_order_passive_long_trade_amount

    def calc_passive_long_trade_price(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
            if self.total_passive_long_trade_volume.get(pair_instrument_key, 0) != 0:
                price = self.total_passive_long_trade_amount[pair_instrument_key] / self.total_passive_long_trade_volume[pair_instrument_key]
                self.total_passive_long_trade_price[pair_instrument_key] = price
            else:
                self.total_passive_long_trade_price[pair_instrument_key] = 0

    def calc_passive_long_trade_volume(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    quant_order_passive_long_trade_volume = quant_order.get_passive_long_trade_volume()
                    if pair_instrument_key not in self.total_passive_long_trade_volume:
                        self.total_passive_long_trade_volume[pair_instrument_key] = quant_order_passive_long_trade_volume
                    else:
                        self.total_passive_long_trade_volume[pair_instrument_key] = self.total_passive_long_trade_volume[pair_instrument_key] + quant_order_passive_long_trade_volume

    def calc_passive_short_trade_amount(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    quant_order_passive_short_trade_amount = quant_order.get_passive_short_trade_amount()
                    if pair_instrument_key not in self.total_passive_short_trade_amount:
                        self.total_passive_short_trade_amount[pair_instrument_key] = quant_order_passive_short_trade_amount
                    else:
                        self.total_passive_short_trade_amount[pair_instrument_key] = self.total_passive_short_trade_amount[pair_instrument_key] + quant_order_passive_short_trade_amount

    def calc_passive_short_trade_price(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
            if self.total_passive_short_trade_volume.get(pair_instrument_key, 0) != 0:
                price = self.total_passive_short_trade_amount[pair_instrument_key] / self.total_passive_short_trade_volume[pair_instrument_key]
                self.total_passive_short_trade_price[pair_instrument_key] = price
            else:
                self.total_passive_short_trade_price[pair_instrument_key] = 0

    def calc_passive_short_trade_volume(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    quant_order_passive_short_trade_volume = quant_order.get_passive_short_trade_volume()
                    if pair_instrument_key not in self.total_passive_short_trade_volume:
                        self.total_passive_short_trade_volume[pair_instrument_key] = quant_order_passive_short_trade_volume
                    else:
                        self.total_passive_short_trade_volume[pair_instrument_key] = self.total_passive_short_trade_volume[pair_instrument_key] + quant_order_passive_short_trade_volume

    def calc_active_slippage(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    active_slippage = quant_order.get_active_slippage()
                    active_volume = quant_order.get_active_volume()
                    if pair_instrument_key not in self.total_active_slippage:
                        self.total_active_slippage[pair_instrument_key] = active_slippage
                        self.total_active_volume[pair_instrument_key] = active_volume
                    else:
                        if (self.total_active_volume[pair_instrument_key] + active_volume) != 0:
                            self.total_active_slippage[pair_instrument_key] = (self.total_active_slippage[pair_instrument_key] * self.total_active_volume[pair_instrument_key] + active_slippage * active_volume) / (self.total_active_volume[pair_instrument_key] + active_volume)
                        self.total_active_volume[pair_instrument_key] = self.total_active_volume[pair_instrument_key] + active_volume

    def calc_passive_slippage(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    passive_slippage = quant_order.get_passive_slippage()
                    passive_volume = quant_order.get_passive_volume()
                    if pair_instrument_key not in self.total_passive_slippage:
                        self.total_passive_slippage[pair_instrument_key] = passive_slippage
                        self.total_passive_volume[pair_instrument_key] = passive_volume
                    else:
                        if (self.total_passive_volume[pair_instrument_key] + passive_volume) != 0:
                            self.total_passive_slippage[pair_instrument_key] = (self.total_passive_slippage[pair_instrument_key] * self.total_passive_volume[pair_instrument_key] + passive_slippage * passive_volume) / (self.total_passive_volume[pair_instrument_key] + passive_volume)
                        self.total_passive_volume[pair_instrument_key] = self.total_passive_volume[pair_instrument_key] + passive_volume

    def calc_active_new_order_system_delay(self):
        system_delay = {}
        order_count = {}
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    new_order_system_delay = quant_order.get_active_new_order_system_delay()
                    if pair_instrument_key not in system_delay:
                        system_delay[pair_instrument_key] = new_order_system_delay
                        order_count[pair_instrument_key] = 1
                    else:
                        system_delay[pair_instrument_key] = system_delay[pair_instrument_key] + new_order_system_delay
                        order_count[pair_instrument_key] = order_count[pair_instrument_key] + 1
        
        for pair_instrument_key in system_delay:
            if order_count[pair_instrument_key] > 0:
                self.avg_active_new_order_system_delay[pair_instrument_key] = system_delay[pair_instrument_key] / order_count[pair_instrument_key]
            else:
                self.avg_active_new_order_system_delay[pair_instrument_key] = 0

    def calc_active_cancel_order_system_delay(self):
        system_delay = {}
        order_count = {}
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    cancel_order_system_delay = quant_order.get_active_cancel_order_system_delay()
                    if pair_instrument_key not in system_delay:
                        system_delay[pair_instrument_key] = cancel_order_system_delay
                        order_count[pair_instrument_key] = 1
                    else:
                        system_delay[pair_instrument_key] = system_delay[pair_instrument_key] + cancel_order_system_delay
                        order_count[pair_instrument_key] = order_count[pair_instrument_key] + 1
        
        for pair_instrument_key in system_delay:
            if order_count[pair_instrument_key] > 0:
                self.avg_active_cancel_order_system_delay[pair_instrument_key] = system_delay[pair_instrument_key] / order_count[pair_instrument_key]
            else:
                self.avg_active_cancel_order_system_delay[pair_instrument_key] = 0

    def calc_active_new_order_exchange_delay(self):
        system_delay = {}
        order_count = {}
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True:
                    new_order_exchange_delay = quant_order.get_active_new_order_exchange_delay()
                    if pair_instrument_key not in system_delay:
                        system_delay[pair_instrument_key] = new_order_exchange_delay
                        order_count[pair_instrument_key] = 1
                    else:
                        system_delay[pair_instrument_key] = system_delay[pair_instrument_key] + new_order_exchange_delay
                        order_count[pair_instrument_key] = order_count[pair_instrument_key] + 1
        
        for pair_instrument_key in system_delay:
            if order_count[pair_instrument_key] > 0:
                self.avg_active_new_order_exchange_delay[pair_instrument_key] = system_delay[pair_instrument_key] / order_count[pair_instrument_key]
            else:
                self.avg_active_new_order_exchange_delay[pair_instrument_key] = 0

    def calc_passive_new_order_system_delay(self):
        system_delay = {}
        order_count = {}
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    new_order_system_delay = quant_order.get_passive_new_order_system_delay()
                    if pair_instrument_key not in system_delay:
                        system_delay[pair_instrument_key] = new_order_system_delay
                        order_count[pair_instrument_key] = 1
                    else:
                        system_delay[pair_instrument_key] = system_delay[pair_instrument_key] + new_order_system_delay
                        order_count[pair_instrument_key] = order_count[pair_instrument_key] + 1
        
        for pair_instrument_key in system_delay:
            if order_count[pair_instrument_key] > 0:
                self.avg_passive_new_order_system_delay[pair_instrument_key] = system_delay[pair_instrument_key] / order_count[pair_instrument_key]
            else:
                self.avg_passive_new_order_system_delay[pair_instrument_key] = 0

    def calc_passive_cancel_order_system_delay(self):
        system_delay = {}
        order_count = {}
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    cancel_order_system_delay = quant_order.get_passive_cancel_order_system_delay()
                    if pair_instrument_key not in system_delay:
                        system_delay[pair_instrument_key] = cancel_order_system_delay
                        order_count[pair_instrument_key] = 1
                    else:
                        system_delay[pair_instrument_key] = system_delay[pair_instrument_key] + cancel_order_system_delay
                        order_count[pair_instrument_key] = order_count[pair_instrument_key] + 1
        
        for pair_instrument_key in system_delay:
            if order_count[pair_instrument_key] > 0:
                self.avg_passive_cancel_order_system_delay[pair_instrument_key] = system_delay[pair_instrument_key] / order_count[pair_instrument_key]
            else:
                self.avg_passive_cancel_order_system_delay[pair_instrument_key] = 0

    def calc_passive_new_order_exchange_delay(self):
        system_delay = {}
        order_count = {}
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is not True:
                    new_order_exchange_delay = quant_order.get_passive_new_order_exchange_delay()
                    if pair_instrument_key not in system_delay:
                        system_delay[pair_instrument_key] = new_order_exchange_delay
                        order_count[pair_instrument_key] = 1
                    else:
                        system_delay[pair_instrument_key] = system_delay[pair_instrument_key] + new_order_exchange_delay
                        order_count[pair_instrument_key] = order_count[pair_instrument_key] + 1
        
        for pair_instrument_key in system_delay:
            if order_count[pair_instrument_key] > 0:
                self.avg_passive_new_order_exchange_delay[pair_instrument_key] = system_delay[pair_instrument_key] / order_count[pair_instrument_key]
            else:
                self.avg_passive_new_order_exchange_delay[pair_instrument_key] = 0

    def calc_total_open_long_target_spread(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]

            traded_flag = False
            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.total_volume_on_order > 0:
                    traded_flag = True

            if traded_flag is False:
                continue


            target_spread = pair_order.get_target_spread('OPEN_LONG')
            target_volume = pair_order.get_target_volume('OPEN_LONG')
            real_volume = pair_order.get_real_volume('OPEN_LONG')
            if real_volume != 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                if pair_instrument_key not in self.total_open_long_target_spread:
                    self.total_open_long_target_spread[pair_instrument_key] = target_spread
                    self.total_open_long_target_volume[pair_instrument_key] = target_volume
                else:
                    if self.total_open_long_target_volume[pair_instrument_key] + target_volume != 0:
                        self.total_open_long_target_spread[pair_instrument_key] = (self.total_open_long_target_spread[pair_instrument_key] * self.total_open_long_target_volume[pair_instrument_key] + target_spread * target_volume) / (self.total_open_long_target_volume[pair_instrument_key] + target_volume)
                    self.total_open_long_target_volume[pair_instrument_key] = self.total_open_long_target_volume[pair_instrument_key] + target_volume

    def calc_total_open_short_target_spread(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]

            traded_flag = False
            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.total_volume_on_order > 0:
                    traded_flag = True

            if traded_flag is False:
                continue

            target_spread = pair_order.get_target_spread('OPEN_SHORT')
            target_volume = pair_order.get_target_volume('OPEN_SHORT')
            real_volume = pair_order.get_real_volume('OPEN_SHORT')
            if real_volume != 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                if pair_instrument_key not in self.total_open_short_target_spread:
                    self.total_open_short_target_spread[pair_instrument_key] = target_spread
                    self.total_open_short_target_volume[pair_instrument_key] = target_volume
                else:
                    if self.total_open_short_target_volume[pair_instrument_key] + target_volume != 0:
                        self.total_open_short_target_spread[pair_instrument_key] = (self.total_open_short_target_spread[pair_instrument_key] * self.total_open_short_target_volume[pair_instrument_key] + target_spread * target_volume) / (self.total_open_short_target_volume[pair_instrument_key] + target_volume)
                    self.total_open_short_target_volume[pair_instrument_key] = self.total_open_short_target_volume[pair_instrument_key] + target_volume

    def calc_total_close_long_target_spread(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]

            traded_flag = False
            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.total_volume_on_order > 0:
                    traded_flag = True

            if traded_flag is False:
                continue

            target_spread = pair_order.get_target_spread('CLOSE_LONG')
            target_volume = pair_order.get_target_volume('CLOSE_LONG')
            real_volume = pair_order.get_real_volume('CLOSE_LONG')
            if real_volume != 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                if pair_instrument_key not in self.total_close_long_target_spread:
                    self.total_close_long_target_spread[pair_instrument_key] = target_spread
                    self.total_close_long_target_volume[pair_instrument_key] = target_volume
                else:
                    if self.total_close_long_target_volume[pair_instrument_key] + target_volume != 0:
                        self.total_close_long_target_spread[pair_instrument_key] = (self.total_close_long_target_spread[pair_instrument_key] * self.total_close_long_target_volume[pair_instrument_key] + target_spread * target_volume) / (self.total_close_long_target_volume[pair_instrument_key] + target_volume)
                    self.total_close_long_target_volume[pair_instrument_key] = self.total_close_long_target_volume[pair_instrument_key] + target_volume

    def calc_total_close_short_target_spread(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]

            traded_flag = False
            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.total_volume_on_order > 0:
                    traded_flag = True

            if traded_flag is False:
                continue

            target_spread = pair_order.get_target_spread('CLOSE_SHORT')
            target_volume = pair_order.get_target_volume('CLOSE_SHORT')
            real_volume = pair_order.get_real_volume('CLOSE_SHORT')
            if real_volume != 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                if pair_instrument_key not in self.total_close_short_target_spread:
                    self.total_close_short_target_spread[pair_instrument_key] = target_spread
                    self.total_close_short_target_volume[pair_instrument_key] = target_volume
                else:
                    if self.total_close_short_target_volume[pair_instrument_key] + target_volume != 0:
                        self.total_close_short_target_spread[pair_instrument_key] = (self.total_close_short_target_spread[pair_instrument_key] * self.total_close_short_target_volume[pair_instrument_key] + target_spread * target_volume) / (self.total_close_short_target_volume[pair_instrument_key] + target_volume)
                    self.total_close_short_target_volume[pair_instrument_key] = self.total_close_short_target_volume[pair_instrument_key] + target_volume

    def calc_total_open_long_real_spread(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]

            traded_flag = False
            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.total_volume_on_order > 0:
                    traded_flag = True

            if traded_flag is False:
                continue

            real_spread = pair_order.get_real_spread('OPEN_LONG')
            real_volume = pair_order.get_real_volume('OPEN_LONG')
            if real_volume != 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                if pair_instrument_key not in self.total_open_long_real_spread:
                    self.total_open_long_real_spread[pair_instrument_key] = real_spread
                    self.total_open_long_real_volume[pair_instrument_key] = real_volume
                else:
                    if self.total_open_long_real_volume[pair_instrument_key] + real_volume != 0:
                        self.total_open_long_real_spread[pair_instrument_key] = (self.total_open_long_real_spread[pair_instrument_key] * self.total_open_long_real_volume[pair_instrument_key] + real_spread * real_volume) / (self.total_open_long_real_volume[pair_instrument_key] + real_volume)
                    self.total_open_long_real_volume[pair_instrument_key] = self.total_open_long_real_volume[pair_instrument_key] + real_volume

    def calc_total_open_short_real_spread(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]

            traded_flag = False
            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.total_volume_on_order > 0:
                    traded_flag = True

            if traded_flag is False:
                continue

            real_spread = pair_order.get_real_spread('OPEN_SHORT')
            real_volume = pair_order.get_real_volume('OPEN_SHORT')
            if real_volume != 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                if pair_instrument_key not in self.total_open_short_real_spread:
                    self.total_open_short_real_spread[pair_instrument_key] = real_spread
                    self.total_open_short_real_volume[pair_instrument_key] = real_volume
                else:
                    if self.total_open_short_real_volume[pair_instrument_key] + real_volume != 0:
                        self.total_open_short_real_spread[pair_instrument_key] = (self.total_open_short_real_spread[pair_instrument_key] * self.total_open_short_real_volume[pair_instrument_key] + real_spread * real_volume) / (self.total_open_short_real_volume[pair_instrument_key] + real_volume)
                    self.total_open_short_real_volume[pair_instrument_key] = self.total_open_short_real_volume[pair_instrument_key] + real_volume

    def calc_total_close_long_real_spread(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]

            traded_flag = False
            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.total_volume_on_order > 0:
                    traded_flag = True

            if traded_flag is False:
                continue

            real_spread = pair_order.get_real_spread('CLOSE_LONG')
            real_volume = pair_order.get_real_volume('CLOSE_LONG')
            if real_volume != 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                if pair_instrument_key not in self.total_close_long_real_spread:
                    self.total_close_long_real_spread[pair_instrument_key] = real_spread
                    self.total_close_long_real_volume[pair_instrument_key] = real_volume
                else:
                    if self.total_close_long_real_volume[pair_instrument_key] + real_volume != 0:
                        self.total_close_long_real_spread[pair_instrument_key] = (self.total_close_long_real_spread[pair_instrument_key] * self.total_close_long_real_volume[pair_instrument_key] + real_spread * real_volume) / (self.total_close_long_real_volume[pair_instrument_key] + real_volume)
                    self.total_close_long_real_volume[pair_instrument_key] = self.total_close_long_real_volume[pair_instrument_key] + real_volume

    def calc_total_close_short_real_spread(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]

            traded_flag = False
            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.total_volume_on_order > 0:
                    traded_flag = True

            if traded_flag is False:
                continue

            real_spread = pair_order.get_real_spread('CLOSE_SHORT')
            real_volume = pair_order.get_real_volume('CLOSE_SHORT')
            if real_volume != 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                if pair_instrument_key not in self.total_close_short_real_spread:
                    self.total_close_short_real_spread[pair_instrument_key] = real_spread
                    self.total_close_short_real_volume[pair_instrument_key] = real_volume
                else:
                    if self.total_close_short_real_volume[pair_instrument_key] + real_volume != 0:
                        self.total_close_short_real_spread[pair_instrument_key] = (self.total_close_short_real_spread[pair_instrument_key] * self.total_close_short_real_volume[pair_instrument_key] + real_spread * real_volume) / (self.total_close_short_real_volume[pair_instrument_key] + real_volume)
                    self.total_close_short_real_volume[pair_instrument_key] = self.total_close_short_real_volume[pair_instrument_key] + real_volume

    def calc_close_long_open_long_real_spead(self):
        for pair_instrument_key in self.total_close_long_real_spread:
            self.close_long_open_long_real_spread[pair_instrument_key] = self.total_close_long_real_spread[pair_instrument_key] - self.total_open_long_real_spread.get(pair_instrument_key, 0)

    def calc_open_short_close_short_real_spread(self):
        for pair_instrument_key in self.total_open_short_real_spread:
            self.open_short_close_short_real_spread[pair_instrument_key] = self.total_open_short_real_spread[pair_instrument_key] - self.total_close_short_real_spread.get(pair_instrument_key, 0)

    def calc_active_long_real_spread(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True and quant_order.direction == 'Direction_LONG':
                    if pair_order.active_total_price_on_order <= 0:
                        continue

                    real_spread = pair_order.passive_total_price_on_order / pair_order.active_total_price_on_order - 1
                    real_volume = pair_order.active_total_volume_on_order

                    if pair_instrument_key not in self.active_long_real_spread:
                        self.active_long_real_spread[pair_instrument_key] = real_spread
                        self.active_long_real_spread_volume[pair_instrument_key] = real_volume
                    else:
                        if self.active_long_real_spread_volume[pair_instrument_key] + real_volume != 0:
                            self.active_long_real_spread[pair_instrument_key] = (self.active_long_real_spread[pair_instrument_key] * self.active_long_real_spread_volume[pair_instrument_key] + real_spread * real_volume) / (self.active_long_real_spread_volume[pair_instrument_key] + real_volume)
                        self.active_long_real_spread_volume[pair_instrument_key] = self.active_long_real_spread_volume[pair_instrument_key] + real_volume

    def calc_active_short_real_spread(self):
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.is_active_order is True and quant_order.direction == 'Direction_SHORT':
                    if pair_order.active_total_price_on_order <= 0:
                        continue

                    real_spread = pair_order.passive_total_price_on_order / pair_order.active_total_price_on_order - 1
                    real_volume = pair_order.active_total_volume_on_order

                    if pair_instrument_key not in self.active_short_real_spread:
                        self.active_short_real_spread[pair_instrument_key] = real_spread
                        self.active_short_real_spread_volume[pair_instrument_key] = real_volume
                    else:
                        if self.active_short_real_spread_volume[pair_instrument_key] + real_volume != 0:
                            self.active_short_real_spread[pair_instrument_key] = (self.active_short_real_spread[pair_instrument_key] * self.active_short_real_spread_volume[pair_instrument_key] + real_spread * real_volume) / (self.active_short_real_spread_volume[pair_instrument_key] + real_volume)
                        self.active_short_real_spread_volume[pair_instrument_key] = self.active_short_real_spread_volume[pair_instrument_key] + real_volume

    def calc_active_depth_delay(self):
        self.system_delay = {}
        self.order_count = {}
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            if abs(pair_order.pair_total_volume) > 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                depth_delay = pair_order.get_active_depth_delay()
                if pair_instrument_key not in self.system_delay:
                    self.system_delay[pair_instrument_key] = depth_delay
                    self.order_count[pair_instrument_key] = 1
                else:
                    self.system_delay[pair_instrument_key] = self.system_delay[pair_instrument_key] + depth_delay
                    self.order_count[pair_instrument_key] = self.order_count[pair_instrument_key] + 1

        for pair_instrument_key in self.system_delay:
            self.avg_active_depth_delay[pair_instrument_key] = self.system_delay[pair_instrument_key] / self.order_count[pair_instrument_key]

    def calc_passive_depth_delay(self):
        self.system_delay = {}
        self.order_count = {}
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            if abs(pair_order.pair_total_volume) > 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                depth_delay = pair_order.get_passive_depth_delay()
                if pair_instrument_key not in self.system_delay:
                    self.system_delay[pair_instrument_key] = depth_delay
                    self.order_count[pair_instrument_key] = 1
                else:
                    self.system_delay[pair_instrument_key] = self.system_delay[pair_instrument_key] + depth_delay
                    self.order_count[pair_instrument_key] = self.order_count[pair_instrument_key] + 1

        for pair_instrument_key in self.system_delay:
            self.avg_passive_depth_delay[pair_instrument_key] = self.system_delay[pair_instrument_key] / self.order_count[pair_instrument_key]


    def calc_active_generate_delay(self):
        self.system_delay = {}
        self.order_count = {}
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            if abs(pair_order.pair_total_volume) > 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                depth_delay = pair_order.get_active_generate_delay()
                if pair_instrument_key not in self.system_delay:
                    self.system_delay[pair_instrument_key] = depth_delay
                    self.order_count[pair_instrument_key] = 1
                else:
                    self.system_delay[pair_instrument_key] = self.system_delay[pair_instrument_key] + depth_delay
                    self.order_count[pair_instrument_key] = self.order_count[pair_instrument_key] + 1

        for pair_instrument_key in self.system_delay:
            self.avg_active_generate_delay[pair_instrument_key] = self.system_delay[pair_instrument_key] / self.order_count[pair_instrument_key]

    def calc_min_active_passive_depth_delay(self):
        system_delay = {}
        order_count = {}
        for pair_id in self.pair_order_dict:
            pair_order = self.pair_order_dict[pair_id]
            if abs(pair_order.pair_total_volume) > 0:
                pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
                active_depth_delay = pair_order.get_active_depth_delay()
                passive_depth_delay = pair_order.get_passive_depth_delay()
                depth_delay = min(active_depth_delay, passive_depth_delay)
                if pair_instrument_key not in system_delay:
                    system_delay[pair_instrument_key] = depth_delay
                    order_count[pair_instrument_key] = 1
                else:
                    system_delay[pair_instrument_key] = system_delay[pair_instrument_key] + depth_delay
                    order_count[pair_instrument_key] = order_count[pair_instrument_key] + 1

        for pair_instrument_key in system_delay:
            self.avg_min_active_passive_depth_delay[pair_instrument_key] = system_delay[pair_instrument_key] / order_count[pair_instrument_key]


    def calc_all(self):
        active_new_order_system_delay_dict = {}
        active_new_order_system_count_dict = {}
        active_cancel_order_system_delay_dict = {}
        active_canecl_order_system_count_dict = {}
        active_new_order_exchange_delay_dict = {}
        active_new_order_exchange_count_dict = {}

        passive_new_order_system_delay_dict = {}
        passive_new_order_system_count_dict = {}
        passive_cancel_order_system_delay_dict = {}
        passive_canecl_order_system_count_dict = {}
        passive_new_order_exchange_delay_dict = {}
        passive_new_order_exchange_count_dict = {}


        active_depth_delay_dict = {}
        active_depth_count_dict = {}
        passive_depth_delay_dict = {}
        passive_depth_count_dict = {}

        active_generate_delay_dict = {}
        active_generate_count_dict = {}
        passive_generate_delay_dict = {}
        passive_generate_count_dict = {}

        pair_order_len = len(self.pair_order_dict)
        cou = 0
        print_flag_10 = False
        print_flag_20 = False
        print_flag_30 = False
        print_flag_40 = False
        print_flag_50 = False
        print_flag_60 = False
        print_flag_70 = False
        print_flag_80 = False
        print_flag_90 = False
        start_time = datetime.now()

        for pair_id in self.pair_order_dict:
            if cou >= 0.1 * pair_order_len and cou < 0.2 * pair_order_len and print_flag_10 is False:
                print_flag_10 = True
                end_time = datetime.now()
                calc_time = end_time - start_time
                print(f'--------------calc---10%-----------time---{calc_time}-------')
            elif cou >= 0.2 * pair_order_len and cou < 0.3 * pair_order_len and print_flag_20 is False:
                print_flag_20 = True
                end_time = datetime.now()
                calc_time = end_time - start_time
                print(f'--------------calc---20%-----------time---{calc_time}-------')
            elif cou >= 0.3 * pair_order_len and cou < 0.4 * pair_order_len and print_flag_30 is False:
                print_flag_30 = True
                end_time = datetime.now()
                calc_time = end_time - start_time
                print(f'--------------calc---30%-----------time---{calc_time}-------')
            elif cou >= 0.4 * pair_order_len and cou < 0.5 * pair_order_len and print_flag_40 is False:
                print_flag_40 = True
                end_time = datetime.now()
                calc_time = end_time - start_time
                print(f'--------------calc---40%-----------time---{calc_time}-------')
            elif cou >= 0.5 * pair_order_len and cou < 0.6 * pair_order_len and print_flag_50 is False:
                print_flag_50 = True
                end_time = datetime.now()
                calc_time = end_time - start_time
                print(f'--------------calc---50%-----------time---{calc_time}-------')
            elif cou >= 0.6 * pair_order_len and cou < 0.7 * pair_order_len and print_flag_60 is False:
                print_flag_60 = True
                end_time = datetime.now()
                calc_time = end_time - start_time
                print(f'--------------calc---60%-----------time---{calc_time}-------')
            elif cou >= 0.7 * pair_order_len and cou < 0.8 * pair_order_len and print_flag_70 is False:
                print_flag_70 = True
                end_time = datetime.now()
                calc_time = end_time - start_time
                print(f'--------------calc---70%-----------time---{calc_time}-------')
            elif cou >= 0.8 * pair_order_len and cou < 0.9 * pair_order_len and print_flag_80 is False:
                print_flag_80 = True
                end_time = datetime.now()
                calc_time = end_time - start_time
                print(f'--------------calc---80%-----------time---{calc_time}-------')
            elif cou >= 0.9 * pair_order_len and cou < pair_order_len and print_flag_90 is False:
                print_flag_90 = True
                end_time = datetime.now()
                calc_time = end_time - start_time
                print(f'--------------calc---90%-----------time---{calc_time}-------')
        

            cou = cou + 1
            pair_order = self.pair_order_dict[pair_id]
            pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'

            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                quant_order_total_trade_amount = quant_order.get_total_trade_amount()

                if quant_order.is_active_order is True:
                    # calc_active_total_trade_amount
                    quant_order_total_trade_amount = quant_order.get_total_trade_amount()
                    if pair_instrument_key not in self.total_active_trade_amount:
                        self.total_active_trade_amount[pair_instrument_key] = quant_order_total_trade_amount
                    else:
                        self.total_active_trade_amount[pair_instrument_key] = self.total_active_trade_amount[pair_instrument_key] + quant_order_total_trade_amount
                
                    # calc_active_total_order_amount
                    quant_order_total_order_amount = quant_order.get_total_order_amount()
                    if pair_instrument_key not in self.total_active_order_amount:
                        self.total_active_order_amount[pair_instrument_key] = quant_order_total_order_amount
                    else:
                        self.total_active_order_amount[pair_instrument_key] = self.total_active_order_amount[pair_instrument_key] + quant_order_total_order_amount

                    # calc_active_trade_order_count
                    quant_order_trade_order_count = quant_order.get_trade_order_count()
                    if pair_instrument_key not in self.total_active_trade_order_count:
                        self.total_active_trade_order_count[pair_instrument_key] = quant_order_trade_order_count
                    else:
                        self.total_active_trade_order_count[pair_instrument_key] = self.total_active_trade_order_count[pair_instrument_key] + quant_order_trade_order_count

                    # calc_active_order_count
                    quant_order_order_count = quant_order.get_order_count()
                    if pair_instrument_key not in self.total_active_order_count:
                        self.total_active_order_count[pair_instrument_key] = quant_order_order_count
                    else:
                        self.total_active_order_count[pair_instrument_key] = self.total_active_order_count[pair_instrument_key] + quant_order_order_count

                    # calc_active_long_trade_amount
                    quant_order_active_long_trade_amount = quant_order.get_active_long_trade_amount()
                    if pair_instrument_key not in self.total_active_long_trade_amount:
                        self.total_active_long_trade_amount[pair_instrument_key] = quant_order_active_long_trade_amount
                    else:
                        self.total_active_long_trade_amount[pair_instrument_key] = self.total_active_long_trade_amount[pair_instrument_key] + quant_order_active_long_trade_amount

                    # calc_active_long_trade_volume
                    quant_order_active_long_trade_volume = quant_order.get_active_long_trade_volume()
                    if pair_instrument_key not in self.total_active_long_trade_volume:
                        self.total_active_long_trade_volume[pair_instrument_key] = quant_order_active_long_trade_volume
                    else:
                        self.total_active_long_trade_volume[pair_instrument_key] = self.total_active_long_trade_volume[pair_instrument_key] + quant_order_active_long_trade_volume

                    # calc_active_short_trade_amount
                    quant_order_active_short_trade_amount = quant_order.get_active_short_trade_amount()
                    if pair_instrument_key not in self.total_active_short_trade_amount:
                        self.total_active_short_trade_amount[pair_instrument_key] = quant_order_active_short_trade_amount
                    else:
                        self.total_active_short_trade_amount[pair_instrument_key] = self.total_active_short_trade_amount[pair_instrument_key] + quant_order_active_short_trade_amount

                    # calc_active_short_trade_volume
                    quant_order_active_short_trade_volume = quant_order.get_active_short_trade_volume()
                    if pair_instrument_key not in self.total_active_short_trade_volume:
                        self.total_active_short_trade_volume[pair_instrument_key] = quant_order_active_short_trade_volume
                    else:
                        self.total_active_short_trade_volume[pair_instrument_key] = self.total_active_short_trade_volume[pair_instrument_key] + quant_order_active_short_trade_volume

                    # calc_active_slippage
                    active_slippage = quant_order.get_active_slippage()
                    active_volume = quant_order.get_active_volume()
                    if pair_instrument_key not in self.total_active_slippage:
                        self.total_active_slippage[pair_instrument_key] = active_slippage
                        self.total_active_volume[pair_instrument_key] = active_volume
                    else:
                        if (self.total_active_volume[pair_instrument_key] + active_volume) != 0:
                            self.total_active_slippage[pair_instrument_key] = (self.total_active_slippage[pair_instrument_key] * self.total_active_volume[pair_instrument_key] + active_slippage * active_volume) / (self.total_active_volume[pair_instrument_key] + active_volume)
                        self.total_active_volume[pair_instrument_key] = self.total_active_volume[pair_instrument_key] + active_volume

                    # calc_active_new_order_system_delay
                    new_order_system_delay = quant_order.get_active_new_order_system_delay()
                    if pair_instrument_key not in active_new_order_system_delay_dict:
                        active_new_order_system_delay_dict[pair_instrument_key] = new_order_system_delay
                        active_new_order_system_count_dict[pair_instrument_key] = 1
                    else:
                        active_new_order_system_delay_dict[pair_instrument_key] = active_new_order_system_delay_dict[pair_instrument_key] + new_order_system_delay
                        active_new_order_system_count_dict[pair_instrument_key] = active_new_order_system_count_dict[pair_instrument_key] + 1

                    # calc_active_cancel_order_system_delay
                    cancel_order_system_delay, flag = quant_order.get_active_cancel_order_system_delay()
                    if flag:
                        if pair_instrument_key not in active_cancel_order_system_delay_dict:
                            active_cancel_order_system_delay_dict[pair_instrument_key] = cancel_order_system_delay
                            active_canecl_order_system_count_dict[pair_instrument_key] = 1
                        else:
                            active_cancel_order_system_delay_dict[pair_instrument_key] = active_cancel_order_system_delay_dict[pair_instrument_key] + cancel_order_system_delay
                            active_canecl_order_system_count_dict[pair_instrument_key] = active_canecl_order_system_count_dict[pair_instrument_key] + 1

                    # calc_active_new_order_exchange_delay
                    new_order_exchange_delay = quant_order.get_active_new_order_exchange_delay()
                    if pair_instrument_key not in active_new_order_exchange_delay_dict:
                        active_new_order_exchange_delay_dict[pair_instrument_key] = new_order_exchange_delay
                        active_new_order_exchange_count_dict[pair_instrument_key] = 1
                    else:
                        active_new_order_exchange_delay_dict[pair_instrument_key] = active_new_order_exchange_delay_dict[pair_instrument_key] + new_order_exchange_delay
                        active_new_order_exchange_count_dict[pair_instrument_key] = active_new_order_exchange_count_dict[pair_instrument_key] + 1

                # calc_active_long_real_spread
                if quant_order.is_active_order is True and quant_order.direction == 'Direction_LONG':
                    if pair_order.active_total_price_on_order > 0:
                        real_spread = 0
                        if pair_order.passive_total_volume_on_order > 0:
                            real_spread = pair_order.passive_total_price_on_order / pair_order.active_total_price_on_order - 1
                        else:
                            real_spread = pair_order.passive_target_price / pair_order.active_total_price_on_order - 1
                        real_volume = pair_order.active_total_volume_on_order
                        if pair_instrument_key not in self.active_long_real_spread:
                            self.active_long_real_spread[pair_instrument_key] = real_spread
                            self.active_long_real_spread_vol[pair_instrument_key] = real_volume
                        else:
                            if self.active_long_real_spread_vol[pair_instrument_key] + real_volume != 0:
                                self.active_long_real_spread[pair_instrument_key] = (self.active_long_real_spread[pair_instrument_key] * self.active_long_real_spread_vol[pair_instrument_key] + real_spread * real_volume) / (self.active_long_real_spread_vol[pair_instrument_key] + real_volume)
                            self.active_long_real_spread_vol[pair_instrument_key] = self.active_long_real_spread_vol[pair_instrument_key] + real_volume

                        if pair_instrument_key not in self.active_long_real_spread_price:
                            self.active_long_real_spread_price[pair_instrument_key] = pair_order.active_total_price_on_order 
                            self.active_long_real_spread_volume[pair_instrument_key] = pair_order.active_total_volume_on_order
                        else:
                            if self.active_long_real_spread_volume[pair_instrument_key] + pair_order.active_total_volume_on_order != 0:
                                self.active_long_real_spread_price[pair_instrument_key] = (self.active_long_real_spread_price[pair_instrument_key] * self.active_long_real_spread_volume[pair_instrument_key] + pair_order.active_total_price_on_order * pair_order.active_total_volume_on_order) / (self.active_long_real_spread_volume[pair_instrument_key] + pair_order.active_total_volume_on_order)
                            self.active_long_real_spread_volume[pair_instrument_key] = self.active_long_real_spread_volume[pair_instrument_key] + pair_order.active_total_volume_on_order

                    if pair_order.passive_total_price_on_order > 0:
                        if pair_instrument_key not in self.passive_long_real_spread_price:
                            self.passive_long_real_spread_price[pair_instrument_key] = pair_order.passive_total_price_on_order 
                            self.passive_long_real_spread_volume[pair_instrument_key] = pair_order.passive_total_volume_on_order
                        else:
                            if self.passive_long_real_spread_volume[pair_instrument_key] + pair_order.passive_total_volume_on_order != 0:
                                self.passive_long_real_spread_price[pair_instrument_key] = (self.passive_long_real_spread_price[pair_instrument_key] * self.passive_long_real_spread_volume[pair_instrument_key] + pair_order.passive_total_price_on_order * pair_order.passive_total_volume_on_order) / (self.passive_long_real_spread_volume[pair_instrument_key] + pair_order.passive_total_volume_on_order)
                            self.passive_long_real_spread_volume[pair_instrument_key] = self.passive_long_real_spread_volume[pair_instrument_key] + pair_order.passive_total_volume_on_order

                # calc_active_short_real_spread
                if quant_order.is_active_order is True and quant_order.direction == 'Direction_SHORT':
                    if pair_order.active_total_price_on_order > 0:
                        real_spread = 0
                        if pair_order.passive_total_volume_on_order > 0:
                            real_spread = pair_order.passive_total_price_on_order / pair_order.active_total_price_on_order - 1
                        else:
                            real_spread = pair_order.passive_target_price / pair_order.active_total_price_on_order - 1

                        real_volume = pair_order.active_total_volume_on_order
                        if pair_instrument_key not in self.active_short_real_spread:
                            self.active_short_real_spread[pair_instrument_key] = real_spread
                            self.active_short_real_spread_vol[pair_instrument_key] = real_volume
                        else:
                            if self.active_short_real_spread_vol[pair_instrument_key] + real_volume != 0:
                                self.active_short_real_spread[pair_instrument_key] = (self.active_short_real_spread[pair_instrument_key] * self.active_short_real_spread_vol[pair_instrument_key] + real_spread * real_volume) / (self.active_short_real_spread_vol[pair_instrument_key] + real_volume)
                            self.active_short_real_spread_vol[pair_instrument_key] = self.active_short_real_spread_vol[pair_instrument_key] + real_volume

                        if pair_instrument_key not in self.active_short_real_spread_price:
                            self.active_short_real_spread_price[pair_instrument_key] = pair_order.active_total_price_on_order
                            self.active_short_real_spread_volume[pair_instrument_key] = pair_order.active_total_volume_on_order 
                        else:
                            if self.active_short_real_spread_volume[pair_instrument_key] + pair_order.active_total_volume_on_order != 0:
                                self.active_short_real_spread_price[pair_instrument_key] = (self.active_short_real_spread_price[pair_instrument_key] * self.active_short_real_spread_volume[pair_instrument_key] + pair_order.active_total_price_on_order * pair_order.active_total_volume_on_order) / (self.active_short_real_spread_volume[pair_instrument_key] + pair_order.active_total_volume_on_order)
                            self.active_short_real_spread_volume[pair_instrument_key] = self.active_short_real_spread_volume[pair_instrument_key] + pair_order.active_total_volume_on_order

                    if pair_order.passive_total_price_on_order > 0:
                        if pair_instrument_key not in self.passive_short_real_spread_price:
                            self.passive_short_real_spread_price[pair_instrument_key] = pair_order.passive_total_price_on_order
                            self.passive_short_real_spread_volume[pair_instrument_key] = pair_order.passive_total_volume_on_order 
                        else:
                            if self.passive_short_real_spread_volume[pair_instrument_key] + pair_order.passive_total_volume_on_order != 0:
                                self.passive_short_real_spread_price[pair_instrument_key] = (self.passive_short_real_spread_price[pair_instrument_key] * self.passive_short_real_spread_volume[pair_instrument_key] + pair_order.passive_total_price_on_order * pair_order.passive_total_volume_on_order) / (self.passive_short_real_spread_volume[pair_instrument_key] + pair_order.passive_total_volume_on_order)
                            self.passive_short_real_spread_volume[pair_instrument_key] = self.passive_short_real_spread_volume[pair_instrument_key] + pair_order.passive_total_volume_on_order

                if quant_order.is_active_order is not True:
                    # calc_passive_total_trade_amount
                    quant_order_total_trade_amount = quant_order.get_total_trade_amount()
                    if pair_instrument_key not in self.total_passive_trade_amount:
                        self.total_passive_trade_amount[pair_instrument_key] = quant_order_total_trade_amount
                    else:
                        self.total_passive_trade_amount[pair_instrument_key] = self.total_passive_trade_amount[pair_instrument_key] + quant_order_total_trade_amount

                    # calc_passive_total_order_amount
                    quant_order_total_order_amount = quant_order.get_total_order_amount()
                    if pair_instrument_key not in self.total_passive_order_amount:
                        self.total_passive_order_amount[pair_instrument_key] = quant_order_total_order_amount
                    else:
                        self.total_passive_order_amount[pair_instrument_key] = self.total_passive_order_amount[pair_instrument_key] + quant_order_total_order_amount

                    # calc_passive_trade_order_count
                    quant_order_trade_order_count = quant_order.get_trade_order_count()
                    if pair_instrument_key not in self.total_passive_trade_order_count:
                        self.total_passive_trade_order_count[pair_instrument_key] = quant_order_trade_order_count
                    else:
                        self.total_passive_trade_order_count[pair_instrument_key] = self.total_passive_trade_order_count[pair_instrument_key] + quant_order_trade_order_count

                    # calc_passive_order_count
                    quant_order_order_count = quant_order.get_order_count()
                    if pair_instrument_key not in self.total_passive_order_count:
                        self.total_passive_order_count[pair_instrument_key] = quant_order_order_count
                    else:
                        self.total_passive_order_count[pair_instrument_key] = self.total_passive_order_count[pair_instrument_key] + quant_order_order_count

                    # calc_passive_long_trade_amount
                    quant_order_passive_long_trade_amount = quant_order.get_passive_long_trade_amount()
                    if pair_instrument_key not in self.total_passive_long_trade_amount:
                        self.total_passive_long_trade_amount[pair_instrument_key] = quant_order_passive_long_trade_amount
                    else:
                        self.total_passive_long_trade_amount[pair_instrument_key] = self.total_passive_long_trade_amount[pair_instrument_key] + quant_order_passive_long_trade_amount

                    # calc_passive_long_trade_volume
                    quant_order_passive_long_trade_volume = quant_order.get_passive_long_trade_volume()
                    if pair_instrument_key not in self.total_passive_long_trade_volume:
                        self.total_passive_long_trade_volume[pair_instrument_key] = quant_order_passive_long_trade_volume
                    else:
                        self.total_passive_long_trade_volume[pair_instrument_key] = self.total_passive_long_trade_volume[pair_instrument_key] + quant_order_passive_long_trade_volume

                    # calc_passive_short_trade_amount
                    quant_order_passive_short_trade_amount = quant_order.get_passive_short_trade_amount()
                    if pair_instrument_key not in self.total_passive_short_trade_amount:
                        self.total_passive_short_trade_amount[pair_instrument_key] = quant_order_passive_short_trade_amount
                    else:
                        self.total_passive_short_trade_amount[pair_instrument_key] = self.total_passive_short_trade_amount[pair_instrument_key] + quant_order_passive_short_trade_amount

                    # calc_passive_short_trade_volume
                    quant_order_passive_short_trade_volume = quant_order.get_passive_short_trade_volume()
                    if pair_instrument_key not in self.total_passive_short_trade_volume:
                        self.total_passive_short_trade_volume[pair_instrument_key] = quant_order_passive_short_trade_volume
                    else:
                        self.total_passive_short_trade_volume[pair_instrument_key] = self.total_passive_short_trade_volume[pair_instrument_key] + quant_order_passive_short_trade_volume

                    # calc_passive_slippage
                    passive_slippage = quant_order.get_passive_slippage()
                    passive_volume = quant_order.get_passive_volume()
                    if pair_instrument_key not in self.total_passive_slippage:
                        self.total_passive_slippage[pair_instrument_key] = passive_slippage
                        self.total_passive_volume[pair_instrument_key] = passive_volume
                    else:
                        if (self.total_passive_volume[pair_instrument_key] + passive_volume) != 0:
                            self.total_passive_slippage[pair_instrument_key] = (self.total_passive_slippage[pair_instrument_key] * self.total_passive_volume[pair_instrument_key] + passive_slippage * passive_volume) / (self.total_passive_volume[pair_instrument_key] + passive_volume)
                        self.total_passive_volume[pair_instrument_key] = self.total_passive_volume[pair_instrument_key] + passive_volume

                    # calc_passive_new_order_system_delay
                    new_order_system_delay = quant_order.get_passive_new_order_system_delay()
                    if pair_instrument_key not in passive_new_order_system_delay_dict:
                        passive_new_order_system_delay_dict[pair_instrument_key] = new_order_system_delay
                        passive_new_order_system_count_dict[pair_instrument_key] = 1
                    else:
                        passive_new_order_system_delay_dict[pair_instrument_key] = passive_new_order_system_delay_dict[pair_instrument_key] + new_order_system_delay
                        passive_new_order_system_count_dict[pair_instrument_key] = passive_new_order_system_count_dict[pair_instrument_key] + 1

                    # calc_passive_cancel_order_system_delay
                    cancel_order_system_delay,flag = quant_order.get_passive_cancel_order_system_delay()
                    if flag:
                        if pair_instrument_key not in passive_cancel_order_system_delay_dict:
                            passive_cancel_order_system_delay_dict[pair_instrument_key] = cancel_order_system_delay
                            passive_canecl_order_system_count_dict[pair_instrument_key] = 1
                        else:
                            passive_cancel_order_system_delay_dict[pair_instrument_key] = passive_cancel_order_system_delay_dict[pair_instrument_key] + cancel_order_system_delay
                            passive_canecl_order_system_count_dict[pair_instrument_key] = passive_canecl_order_system_count_dict[pair_instrument_key] + 1

                    # calc_passive_new_order_exchange_delay
                    new_order_exchange_delay = quant_order.get_passive_new_order_exchange_delay()
                    if pair_instrument_key not in passive_new_order_exchange_delay_dict:
                        passive_new_order_exchange_delay_dict[pair_instrument_key] = new_order_exchange_delay
                        passive_new_order_exchange_count_dict[pair_instrument_key] = 1
                    else:
                        passive_new_order_exchange_delay_dict[pair_instrument_key] = passive_new_order_exchange_delay_dict[pair_instrument_key] + new_order_exchange_delay
                        passive_new_order_exchange_count_dict[pair_instrument_key] = passive_new_order_exchange_count_dict[pair_instrument_key] + 1


            traded_flag = False
            all_quant_orders = self.quant_order_statictis.get_all_quant_order_by_pair_id(pair_id)
            for strategy_order_id in all_quant_orders:
                quant_order = all_quant_orders[strategy_order_id]
                if quant_order.total_volume_on_order > 0:
                    traded_flag = True
                    break

            if traded_flag is True:
                # calc_total_open_long_target_spread
                target_spread = pair_order.get_target_spread('OPEN_LONG')
                target_volume = pair_order.get_target_volume('OPEN_LONG')
                real_volume = pair_order.get_real_volume('OPEN_LONG')
                if real_volume != 0:
                    if pair_instrument_key not in self.total_open_long_target_spread:
                        self.total_open_long_target_spread[pair_instrument_key] = target_spread
                        self.total_open_long_target_volume[pair_instrument_key] = target_volume
                    else:
                        if self.total_open_long_target_volume[pair_instrument_key] + target_volume != 0:
                            self.total_open_long_target_spread[pair_instrument_key] = (self.total_open_long_target_spread[pair_instrument_key] * self.total_open_long_target_volume[pair_instrument_key] + target_spread * target_volume) / (self.total_open_long_target_volume[pair_instrument_key] + target_volume)
                        self.total_open_long_target_volume[pair_instrument_key] = self.total_open_long_target_volume[pair_instrument_key] + target_volume

                # calc_total_open_short_target_spread
                target_spread = pair_order.get_target_spread('OPEN_SHORT')
                target_volume = pair_order.get_target_volume('OPEN_SHORT')
                real_volume = pair_order.get_real_volume('OPEN_SHORT')
                if real_volume != 0:
                    if pair_instrument_key not in self.total_open_short_target_spread:
                        self.total_open_short_target_spread[pair_instrument_key] = target_spread
                        self.total_open_short_target_volume[pair_instrument_key] = target_volume
                    else:
                        if self.total_open_short_target_volume[pair_instrument_key] + target_volume != 0:
                            self.total_open_short_target_spread[pair_instrument_key] = (self.total_open_short_target_spread[pair_instrument_key] * self.total_open_short_target_volume[pair_instrument_key] + target_spread * target_volume) / (self.total_open_short_target_volume[pair_instrument_key] + target_volume)
                        self.total_open_short_target_volume[pair_instrument_key] = self.total_open_short_target_volume[pair_instrument_key] + target_volume


                # calc_total_close_long_target_spread
                target_spread = pair_order.get_target_spread('CLOSE_LONG')
                target_volume = pair_order.get_target_volume('CLOSE_LONG')
                real_volume = pair_order.get_real_volume('CLOSE_LONG')
                if real_volume != 0:
                    if pair_instrument_key not in self.total_close_long_target_spread:
                        self.total_close_long_target_spread[pair_instrument_key] = target_spread
                        self.total_close_long_target_volume[pair_instrument_key] = target_volume
                    else:
                        if self.total_close_long_target_volume[pair_instrument_key] + target_volume != 0:
                            self.total_close_long_target_spread[pair_instrument_key] = (self.total_close_long_target_spread[pair_instrument_key] * self.total_close_long_target_volume[pair_instrument_key] + target_spread * target_volume) / (self.total_close_long_target_volume[pair_instrument_key] + target_volume)
                        self.total_close_long_target_volume[pair_instrument_key] = self.total_close_long_target_volume[pair_instrument_key] + target_volume

                # calc_total_close_short_target_spread
                target_spread = pair_order.get_target_spread('CLOSE_SHORT')
                target_volume = pair_order.get_target_volume('CLOSE_SHORT')
                real_volume = pair_order.get_real_volume('CLOSE_SHORT')
                if real_volume != 0:
                    if pair_instrument_key not in self.total_close_short_target_spread:
                        self.total_close_short_target_spread[pair_instrument_key] = target_spread
                        self.total_close_short_target_volume[pair_instrument_key] = target_volume
                    else:
                        if self.total_close_short_target_volume[pair_instrument_key] + target_volume != 0:
                            self.total_close_short_target_spread[pair_instrument_key] = (self.total_close_short_target_spread[pair_instrument_key] * self.total_close_short_target_volume[pair_instrument_key] + target_spread * target_volume) / (self.total_close_short_target_volume[pair_instrument_key] + target_volume)
                        self.total_close_short_target_volume[pair_instrument_key] = self.total_close_short_target_volume[pair_instrument_key] + target_volume

                # calc_total_open_long_real_spread
                real_spread = pair_order.get_real_spread('OPEN_LONG')
                real_volume = pair_order.get_real_volume('OPEN_LONG')
                if real_volume != 0:
                    if pair_instrument_key not in self.total_open_long_real_spread:
                        self.total_open_long_real_spread[pair_instrument_key] = real_spread
                        self.total_open_long_real_volume[pair_instrument_key] = real_volume
                    else:
                        if self.total_open_long_real_volume[pair_instrument_key] + real_volume != 0:
                            self.total_open_long_real_spread[pair_instrument_key] = (self.total_open_long_real_spread[pair_instrument_key] * self.total_open_long_real_volume[pair_instrument_key] + real_spread * real_volume) / (self.total_open_long_real_volume[pair_instrument_key] + real_volume)
                        self.total_open_long_real_volume[pair_instrument_key] = self.total_open_long_real_volume[pair_instrument_key] + real_volume

                # calc_total_open_short_real_spread
                real_spread = pair_order.get_real_spread('OPEN_SHORT')
                real_volume = pair_order.get_real_volume('OPEN_SHORT')
                if real_volume != 0:
                    if pair_instrument_key not in self.total_open_short_real_spread:
                        self.total_open_short_real_spread[pair_instrument_key] = real_spread
                        self.total_open_short_real_volume[pair_instrument_key] = real_volume
                    else:
                        if self.total_open_short_real_volume[pair_instrument_key] + real_volume != 0:
                            self.total_open_short_real_spread[pair_instrument_key] = (self.total_open_short_real_spread[pair_instrument_key] * self.total_open_short_real_volume[pair_instrument_key] + real_spread * real_volume) / (self.total_open_short_real_volume[pair_instrument_key] + real_volume)
                        self.total_open_short_real_volume[pair_instrument_key] = self.total_open_short_real_volume[pair_instrument_key] + real_volume

                # calc_total_close_long_real_spread
                real_spread = pair_order.get_real_spread('CLOSE_LONG')
                real_volume = pair_order.get_real_volume('CLOSE_LONG')
                if real_volume != 0:
                    if pair_instrument_key not in self.total_close_long_real_spread:
                        self.total_close_long_real_spread[pair_instrument_key] = real_spread
                        self.total_close_long_real_volume[pair_instrument_key] = real_volume
                    else:
                        if self.total_close_long_real_volume[pair_instrument_key] + real_volume != 0:
                            self.total_close_long_real_spread[pair_instrument_key] = (self.total_close_long_real_spread[pair_instrument_key] * self.total_close_long_real_volume[pair_instrument_key] + real_spread * real_volume) / (self.total_close_long_real_volume[pair_instrument_key] + real_volume)
                        self.total_close_long_real_volume[pair_instrument_key] = self.total_close_long_real_volume[pair_instrument_key] + real_volume

                # calc_total_close_short_real_spread
                real_spread = pair_order.get_real_spread('CLOSE_SHORT')
                real_volume = pair_order.get_real_volume('CLOSE_SHORT')
                if real_volume != 0:
                    if pair_instrument_key not in self.total_close_short_real_spread:
                        self.total_close_short_real_spread[pair_instrument_key] = real_spread
                        self.total_close_short_real_volume[pair_instrument_key] = real_volume
                    else:
                        if self.total_close_short_real_volume[pair_instrument_key] + real_volume != 0:
                            self.total_close_short_real_spread[pair_instrument_key] = (self.total_close_short_real_spread[pair_instrument_key] * self.total_close_short_real_volume[pair_instrument_key] + real_spread * real_volume) / (self.total_close_short_real_volume[pair_instrument_key] + real_volume)
                        self.total_close_short_real_volume[pair_instrument_key] = self.total_close_short_real_volume[pair_instrument_key] + real_volume

            # calc_active_depth_delay
            if abs(pair_order.pair_total_volume) > 0:
                depth_delay = pair_order.get_active_depth_delay()
                if pair_instrument_key not in active_depth_delay_dict:
                    active_depth_delay_dict[pair_instrument_key] = depth_delay
                    active_depth_count_dict[pair_instrument_key] = 1
                else:
                    active_depth_delay_dict[pair_instrument_key] = active_depth_delay_dict[pair_instrument_key] + depth_delay
                    active_depth_count_dict[pair_instrument_key] = active_depth_count_dict[pair_instrument_key] + 1

            # calc_passive_depth_delay
            if abs(pair_order.pair_total_volume) > 0:
                depth_delay = pair_order.get_passive_depth_delay()
                if pair_instrument_key not in passive_depth_delay_dict:
                    passive_depth_delay_dict[pair_instrument_key] = depth_delay
                    passive_depth_count_dict[pair_instrument_key] = 1
                else:
                    passive_depth_delay_dict[pair_instrument_key] = passive_depth_delay_dict[pair_instrument_key] + depth_delay
                    passive_depth_count_dict[pair_instrument_key] = passive_depth_count_dict[pair_instrument_key] + 1

            # calc_active_generate_delay
            if abs(pair_order.pair_total_volume) > 0:
                depth_delay = pair_order.get_active_generate_delay()
                if pair_instrument_key not in active_generate_delay_dict:
                    active_generate_delay_dict[pair_instrument_key] = depth_delay
                    active_generate_count_dict[pair_instrument_key] = 1
                else:
                    active_generate_delay_dict[pair_instrument_key] = active_generate_delay_dict[pair_instrument_key] + depth_delay
                    active_generate_count_dict[pair_instrument_key] = active_generate_count_dict[pair_instrument_key] + 1

            # calc_min_active_passive_depth_delay
            if abs(pair_order.pair_total_volume) > 0:
                active_depth_delay = pair_order.get_active_depth_delay()
                passive_depth_delay = pair_order.get_passive_depth_delay()
                depth_delay = min(active_depth_delay, passive_depth_delay)
                if pair_instrument_key not in passive_generate_delay_dict:
                    passive_generate_delay_dict[pair_instrument_key] = depth_delay
                    passive_generate_count_dict[pair_instrument_key] = 1
                else:
                    passive_generate_delay_dict[pair_instrument_key] = passive_generate_delay_dict[pair_instrument_key] + depth_delay
                    passive_generate_count_dict[pair_instrument_key] = passive_generate_count_dict[pair_instrument_key] + 1



        # calc_active_trade_rate
        for pair_instrument_key in self.total_active_order_count:
            if self.total_active_order_count[pair_instrument_key] > 0:
                self.total_active_trade_rate[pair_instrument_key] = self.total_active_trade_order_count.get(pair_instrument_key, 0) / self.total_active_order_count[pair_instrument_key]

        # calc_passive_trade_rate
        for pair_instrument_key in self.total_passive_order_count:
            if self.total_passive_order_count[pair_instrument_key] > 0:
                self.total_passive_trade_rate[pair_instrument_key] = self.total_passive_trade_order_count.get(pair_instrument_key, 0) / self.total_passive_order_count[pair_instrument_key]

        # calc_active_long_trade_price
        for pair_instrument_key in self.total_active_long_trade_amount:
            if self.total_active_long_trade_volume.get(pair_instrument_key, 0) != 0:
                price = self.total_active_long_trade_amount[pair_instrument_key] / self.total_active_long_trade_volume[pair_instrument_key]
                self.total_active_long_trade_price[pair_instrument_key] = price
            else:
                self.total_active_long_trade_price[pair_instrument_key] = 0

        # calc_active_short_trade_price
        for pair_instrument_key in self.total_active_short_trade_amount:
            if self.total_active_short_trade_volume.get(pair_instrument_key, 0) != 0:
                price = self.total_active_short_trade_amount[pair_instrument_key] / self.total_active_short_trade_volume[pair_instrument_key]
                self.total_active_short_trade_price[pair_instrument_key] = price
            else:
                self.total_active_short_trade_price[pair_instrument_key] = 0

        # calc_passive_long_trade_price
        for pair_instrument_key in self.total_passive_long_trade_amount:
            if self.total_passive_long_trade_volume.get(pair_instrument_key, 0) != 0:
                price = self.total_passive_long_trade_amount[pair_instrument_key] / self.total_passive_long_trade_volume[pair_instrument_key]
                self.total_passive_long_trade_price[pair_instrument_key] = price
            else:
                self.total_passive_long_trade_price[pair_instrument_key] = 0

        # calc_passive_short_trade_price
        for pair_instrument_key in self.total_passive_short_trade_amount:
            if self.total_passive_short_trade_volume.get(pair_instrument_key, 0) != 0:
                price = self.total_passive_short_trade_amount[pair_instrument_key] / self.total_passive_short_trade_volume[pair_instrument_key]
                self.total_passive_short_trade_price[pair_instrument_key] = price
            else:
                self.total_passive_short_trade_price[pair_instrument_key] = 0

        # calc_active_new_order_system_delay
        for pair_instrument_key in active_new_order_system_delay_dict:
            if active_new_order_system_count_dict[pair_instrument_key] > 0:
                self.avg_active_new_order_system_delay[pair_instrument_key] = active_new_order_system_delay_dict[pair_instrument_key] / active_new_order_system_count_dict[pair_instrument_key]
            else:
                self.avg_active_new_order_system_delay[pair_instrument_key] = 0

        # calc_active_cancel_order_system_delay
        for pair_instrument_key in active_cancel_order_system_delay_dict:
            if active_canecl_order_system_count_dict[pair_instrument_key] > 0:
                self.avg_active_cancel_order_system_delay[pair_instrument_key] = active_cancel_order_system_delay_dict[pair_instrument_key] / active_canecl_order_system_count_dict[pair_instrument_key]
            else:
                self.avg_active_cancel_order_system_delay[pair_instrument_key] = 0

        # calc_active_new_order_exchange_delay
        for pair_instrument_key in active_new_order_exchange_delay_dict:
            if active_new_order_exchange_count_dict[pair_instrument_key] > 0:
                self.avg_active_new_order_exchange_delay[pair_instrument_key] = active_new_order_exchange_delay_dict[pair_instrument_key] / active_new_order_exchange_count_dict[pair_instrument_key]
            else:
                self.avg_active_new_order_exchange_delay[pair_instrument_key] = 0

        # calc_passive_new_order_system_delay
        for pair_instrument_key in passive_new_order_system_delay_dict:
            if passive_new_order_system_count_dict[pair_instrument_key] > 0:
                self.avg_passive_new_order_system_delay[pair_instrument_key] = passive_new_order_system_delay_dict[pair_instrument_key] / passive_new_order_system_count_dict[pair_instrument_key]
            else:
                self.avg_passive_new_order_system_delay[pair_instrument_key] = 0

        # calc_passive_cancel_order_system_delay
        for pair_instrument_key in passive_cancel_order_system_delay_dict:
            if passive_canecl_order_system_count_dict[pair_instrument_key] > 0:
                self.avg_passive_cancel_order_system_delay[pair_instrument_key] = passive_cancel_order_system_delay_dict[pair_instrument_key] / passive_canecl_order_system_count_dict[pair_instrument_key]
            else:
                self.avg_passive_cancel_order_system_delay[pair_instrument_key] = 0

        # calc_passive_new_order_exchange_delay
        for pair_instrument_key in passive_new_order_system_delay_dict:
            if passive_new_order_exchange_count_dict[pair_instrument_key] > 0:
                self.avg_passive_new_order_exchange_delay[pair_instrument_key] = passive_new_order_exchange_delay_dict[pair_instrument_key] / passive_new_order_exchange_count_dict[pair_instrument_key]
            else:
                self.avg_passive_new_order_exchange_delay[pair_instrument_key] = 0

        # calc_close_long_open_long_real_spead
        for pair_instrument_key in self.total_close_long_real_spread:
            self.close_long_open_long_real_spread[pair_instrument_key] = self.total_close_long_real_spread[pair_instrument_key] - self.total_open_long_real_spread.get(pair_instrument_key, 0)

        # calc_open_short_close_short_real_spread
        for pair_instrument_key in self.total_open_short_real_spread:
            self.open_short_close_short_real_spread[pair_instrument_key] = self.total_open_short_real_spread[pair_instrument_key] - self.total_close_short_real_spread.get(pair_instrument_key, 0)

        # calc_active_depth_delay
        for pair_instrument_key in active_depth_delay_dict:
            self.avg_active_depth_delay[pair_instrument_key] = active_depth_delay_dict[pair_instrument_key] / active_depth_count_dict[pair_instrument_key]

        # calc_passive_depth_delay
        for pair_instrument_key in passive_depth_delay_dict:
            self.avg_passive_depth_delay[pair_instrument_key] = passive_depth_delay_dict[pair_instrument_key] / passive_depth_count_dict[pair_instrument_key]

        # calc_active_generate_delay
        for pair_instrument_key in active_generate_delay_dict:
            self.avg_active_generate_delay[pair_instrument_key] = active_generate_delay_dict[pair_instrument_key] / active_generate_count_dict[pair_instrument_key]

        # calc_min_active_passive_depth_delay

        for pair_instrument_key in passive_generate_delay_dict:
            self.avg_min_active_passive_depth_delay[pair_instrument_key] = passive_generate_delay_dict[pair_instrument_key] / passive_generate_count_dict[pair_instrument_key]
