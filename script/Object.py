import sys
VERSION=''
with open('/inc/version.inc') as f:
    lines = f.readlines()
    VERSION = lines[0].split('=')[1].strip()
sys.path.append('/opt/version/'+VERSION)
sys.path.append('/opt/version/'+VERSION+'/python-libs')
from securitymanager.pysecuritymanager import SecurityManager as SMC


smc = SMC(ip="127.0.0.1", port=9379, passwd="refZa2KcTj$QjyXk")

class QuantOrder:
    def __init__(self):
        self.strategy_name = ''
        self.strategy_order_id = 0
        self.system_order_id = 0
        self.exchange_order_id = ''
        self.instrument_key = ''
        self.instrument = ''
        self.order_type = ''
        self.direction = ''
        self.order_status = ''
        self.order_time_status = []
        self.target_price = 0
        self.price = 0
        self.volume = 0
        self.total_price_on_order = 0
        self.total_volume_on_order = 0
        self.trade_volume = 0
        self.active_bid_price_1 = 0
        self.active_bid_volume_1 = 0
        self.active_ask_price_1 = 0
        self.active_ask_volume_1 = 0
        self.passive_bid_price_1 = 0
        self.passive_bid_volume_1 = 0
        self.passive_ask_price_1 = 0
        self.passive_ask_volume_1 = 0
        self.total_short_fee = 0
        self.total_long_fee = 0
        self.order_time = 0
        self.update_time = 0
        self.kill_time = 0
        self.error_id = 0
        self.origin_error_msg = ''
        self.reduce_only = False
        self.pair_id = 0
        self.algo_pair_id = 0
        self.is_active_order = False
        self.rebalance = False
        self.value = 0

    def init(self):
        inst_list = self.instrument_key.split('.')
        if len(inst_list) >= 3:
            exchange = inst_list[0]
            inst_type = inst_list[1]
            self.instrument = inst_list[2]
            ok, info = smc.get_instrument_info(exchange, inst_type, self.instrument)
            if ok:
                self.value = info['value']

    def get_total_trade_amount(self):
        total_trade_amount = self.total_price_on_order * self.total_volume_on_order * self.value
        return total_trade_amount

    def get_total_order_amount(self):
        total_order_amount = self.price * self.volume * self.value
        return total_order_amount

    def get_trade_order_count(self):
        num = 0
        if self.total_volume_on_order > 0:
            num = 1
        return num

    def get_order_count(self):
        num = 1
        return num

    def get_active_long_trade_amount(self):
        if self.is_active_order is True:
            if self.direction == 'Direction_LONG':
                return self.get_total_trade_amount()
            else:
                 return 0
        else:
            return 0

    def get_active_long_trade_price(self):
        if self.is_active_order is True:
            if self.direction == 'Direction_LONG':
                return self.total_price_on_order
            else:
                 return 0
        else:
            return 0

    def get_active_long_trade_volume(self):
        if self.is_active_order is True:
            if self.direction == 'Direction_LONG':
                return self.total_volume_on_order
            else:
                 return 0
        else:
            return 0

    def get_active_short_trade_amount(self):
        if self.is_active_order is True:
            if self.direction == 'Direction_SHORT':
                return self.get_total_trade_amount()
            else:
                 return 0
        else:
            return 0

    def get_active_short_trade_price(self):
        if self.is_active_order is True:
            if self.direction == 'Direction_SHORT':
                return self.total_price_on_order
            else:
                 return 0
        else:
            return 0

    def get_active_short_trade_volume(self):
        if self.is_active_order is True:
            if self.direction == 'Direction_SHORT':
                return self.total_volume_on_order
            else:
                 return 0
        else:
            return 0

    def get_passive_long_trade_amount(self):
        if self.is_active_order is not True:
            if self.direction == 'Direction_LONG':
                return self.get_total_trade_amount()
            else:
                 return 0
        else:
            return 0

    def get_passive_long_trade_price(self):
        if self.is_active_order is not True:
            if self.direction == 'Direction_LONG':
                return self.total_price_on_order
            else:
                 return 0
        else:
            return 0

    def get_passive_long_trade_volume(self):
        if self.is_active_order is not True:
            if self.direction == 'Direction_LONG':
                return self.total_volume_on_order
            else:
                 return 0
        else:
            return 0

    def get_passive_short_trade_amount(self):
        if self.is_active_order is not True:
            if self.direction == 'Direction_SHORT':
                return self.get_total_trade_amount()
            else:
                 return 0
        else:
            return 0

    def get_passive_short_trade_price(self):
        if self.is_active_order is not True:
            if self.direction == 'Direction_SHORT':
                return self.total_price_on_order
            else:
                 return 0
        else:
            return 0

    def get_passive_short_trade_volume(self):
        if self.is_active_order is not True:
            if self.direction == 'Direction_SHORT':
                return self.total_volume_on_order
            else:
                 return 0
        else:
            return 0

    def get_active_slippage(self):
        slippage = 0
        if self.is_active_order is True and self.total_price_on_order > 0:
            if self.direction == 'Direction_LONG':
                slippage = self.total_price_on_order / self.target_price - 1
            else:
                slippage = self.target_price / self.total_price_on_order - 1
        return slippage

    def get_active_volume(self):
        volume = 0
        if self.is_active_order is True:
            volume = self.total_volume_on_order
        return volume

    def get_passive_slippage(self):
        slippage = 0
        if self.is_active_order is not True and self.total_price_on_order > 0:
            if self.direction == 'Direction_LONG':
                slippage = self.total_price_on_order / self.target_price - 1
            else:
                slippage = self.target_price / self.total_price_on_order - 1
        return slippage

    def get_passive_volume(self):
        volume = 0
        if self.is_active_order is not True:
            volume = self.total_volume_on_order
        return volume

    def get_active_new_order_system_delay(self):
        pend_new_time = 0
        pending_new_time = 0
        pend_new_to_pending_new_time = 0

        if self.is_active_order is True:
            for time_status in self.order_time_status:
                time_status_list = time_status.split('-')
                if len(time_status_list) < 2:
                    continue
    
                t = int(time_status_list[0])
                status = time_status_list[1]

                if status == 'OrderStatus_PEND_NEW':
                    pend_new_time = t
                elif status == 'OrderStatus_PENDING_NEW':
                    pending_new_time = t

            if pend_new_time > 0 and pending_new_time > 0:
                pend_new_to_pending_new_time = (pending_new_time - pend_new_time) / 1000
        return pend_new_to_pending_new_time

    def get_active_cancel_order_system_delay(self):
        cancel_time = 0
        cancelling_time = 0
        cancel_to_cancelling_time = 0
        flag = False

        if self.is_active_order is True:
            for time_status in self.order_time_status:
                time_status_list = time_status.split('-')
                if len(time_status_list) < 2:
                    continue
    
                t = int(time_status_list[0])
                status = time_status_list[1]

                if status == 'OrderStatus_CANCEL':
                    cancel_time = t
                elif status == 'OrderStatus_CANCELLING':
                    cancelling_time = t

            if cancel_time > 0 and cancelling_time > 0:
                cancel_to_cancelling_time = (cancelling_time - cancel_time) / 1000
                flag = True
        return cancel_to_cancelling_time, flag

    def get_active_new_order_exchange_delay(self):
        pending_new_time = 0
        exchange_order_time = 0
        pend_new_to_exchange_order_time = 0

        if self.is_active_order is True:
            for time_status in self.order_time_status:
                time_status_list = time_status.split('-')
                if len(time_status_list) < 2:
                    continue
    
                t = int(time_status_list[0])
                status = time_status_list[1]

                if status == 'OrderStatus_PENDING_NEW':
                    pending_new_time = t
                elif status == 'OrderStatus_NEW':
                    exchange_order_time = t
                    break
                elif status == 'OrderStatus_PARTFILLED':
                    exchange_order_time = t
                    break
                elif status == 'OrderStatus_FILLED':
                    exchange_order_time = t
                    break
                elif status == 'OrderStatus_CANCELED':
                    exchange_order_time = t
                    break

            if pending_new_time > 0 and exchange_order_time > 0:
                pend_new_to_exchange_order_time = (exchange_order_time - pending_new_time) / 1000
        return pend_new_to_exchange_order_time

    def get_passive_new_order_system_delay(self):
        pend_new_time = 0
        pending_new_time = 0
        pend_new_to_pending_new_time = 0

        if self.is_active_order is not True:
            for time_status in self.order_time_status:
                time_status_list = time_status.split('-')
                if len(time_status_list) < 2:
                    continue
    
                t = int(time_status_list[0])
                status = time_status_list[1]

                if status == 'OrderStatus_PEND_NEW':
                    pend_new_time = t
                elif status == 'OrderStatus_PENDING_NEW':
                    pending_new_time = t

            if pend_new_time > 0 and pending_new_time > 0:
                pend_new_to_pending_new_time = (pending_new_time - pend_new_time) / 1000
        return pend_new_to_pending_new_time

    def get_passive_cancel_order_system_delay(self):
        cancel_time = 0
        cancelling_time = 0
        cancel_to_cancelling_time = 0
        flag = False

        if self.is_active_order is not True:
            for time_status in self.order_time_status:
                time_status_list = time_status.split('-')
                if len(time_status_list) < 2:
                    continue
    
                t = int(time_status_list[0])
                status = time_status_list[1]

                if status == 'OrderStatus_CANCEL':
                    cancel_time = t
                elif status == 'OrderStatus_CANCELLING':
                    cancelling_time = t

            if cancel_time > 0 and cancelling_time > 0:
                cancel_to_cancelling_time = (cancelling_time - cancel_time) / 1000
                flag = True
        return cancel_to_cancelling_time, flag

    def get_passive_new_order_exchange_delay(self):
        pending_new_time = 0
        exchange_order_time = 0
        pend_new_to_exchange_order_time = 0

        if self.is_active_order is not True:
            for time_status in self.order_time_status:
                time_status_list = time_status.split('-')
                if len(time_status_list) < 2:
                    continue
    
                t = int(time_status_list[0])
                status = time_status_list[1]

                if status == 'OrderStatus_PENDING_NEW':
                    pending_new_time = t
                elif status == 'OrderStatus_NEW':
                    exchange_order_time = t
                    break
                elif status == 'OrderStatus_PARTFILLED':
                    exchange_order_time = t
                    break
                elif status == 'OrderStatus_FILLED':
                    exchange_order_time = t
                    break
                elif status == 'OrderStatus_CANCELED':
                    exchange_order_time = t
                    break

            if pending_new_time > 0 and exchange_order_time > 0:
                pend_new_to_exchange_order_time = (exchange_order_time - pending_new_time) / 1000
        return pend_new_to_exchange_order_time


class PairOrder:
    def __init__(self):
        self.pair_id = 0
        self.algo_pair_id = 0
        self.strategy_name = ''
        self.base_asset = ''
        self.trading_type_order = ''
        self.trading_type_offset = ''
        self.target_volume = 0
        self.active_instrument_key = ''
        self.active_direction = ''
        self.active_target_price = 0
        self.active_bid_price_1 = 0
        self.active_bid_volume_1 = 0
        self.active_ask_price_1 = 0
        self.active_ask_volume_1 = 0
        self.passive_instrument_key = ''
        self.passive_direction = ''
        self.passive_target_price = 0
        self.passive_bid_price_1 = 0
        self.passive_bid_volume_1 = 0
        self.passive_ask_price_1 = 0
        self.passive_ask_volume_1 = 0
        self.spread_bid_ask = 0
        self.spread_bid_bid = 0
        self.spread_ask_bid = 0
        self.spread_ask_ask = 0
        self.generate_ts = 0
        self.active_depth_ts = 0
        self.passive_depth_ts = 0
        self.active_total_price_on_order = 0
        self.active_total_volume_on_order = 0
        self.passive_total_price_on_order = 0
        self.passive_total_volume_on_order = 0
        self.pair_total_volume = 0
        self.pair_active_total_price = 0
        self.pair_passive_total_price = 0
        self.active_frozen_price = 0
        self.active_frozen_volume = 0
        self.passive_frozen_price = 0
        self.passive_frozen_volume = 0
        self.active_account_id = 0
        self.passive_account_id = 0
        self.status = 0
        self.rebalance_flag = False
        self.create_time = 0
        self.update_time = 0

    def get_target_spread(self, trading_type_offset):
        spread = 0
        if self.trading_type_offset == trading_type_offset:
            spread = self.passive_target_price / self.active_target_price - 1
        return spread

    def get_target_volume(self, trading_type_offset):
        volume = 0
        if self.trading_type_offset == trading_type_offset:
            volume = self.active_total_volume_on_order
        return volume

    def get_real_spread(self, trading_type_offset):
        spread = 0
        if self.trading_type_offset == trading_type_offset and self.active_total_price_on_order != 0:
            spread = self.passive_total_price_on_order / self.active_total_price_on_order - 1
        return spread

    def get_real_volume(self, trading_type_offset):
        volume = 0
        if self.trading_type_offset == trading_type_offset:
            volume = self.active_total_volume_on_order
        return volume

    def get_active_depth_delay(self):
        delay = (self.create_time - self.active_depth_ts) / 1000
        return delay

    def get_active_generate_delay(self):
        delay = (self.create_time - self.generate_ts) / 1000
        return delay

    def get_passive_depth_delay(self):
        delay = (self.create_time - self.passive_depth_ts) / 1000
        return delay


class AlgoPairOrder:
    def __init__(self):
        self.algo_type = ''
        self.algo_strategy_name = ''
        self.algo_order_id = 0
        self.pair_instrument_key = ''
        self.base_asset = ''
        self.algo_order_status = ''
        self.active_instrument_key = ''
        self.active_price_taker_pct = 0
        self.active_price_maker_pct = 0
        self.active_account_id = 0
        self.active_drive_type = ''
        self.active_depth_maker_check = False
        self.active_depth_taker_check = False
        self.active_depth_maker_check_type = ''
        self.active_depth_taker_check_type = ''
        self.active_order_type = ''
        self.passive_instrument_key = ''
        self.passive_price_taker_pct = 0
        self.passive_price_maker_pct = 0
        self.passive_account_id = 0
        self.passive_drive_type = ''
        self.passive_depth_maker_check = False
        self.passive_depth_taker_check = False
        self.passive_depth_maker_check_type = ''
        self.passive_depth_taker_check_type = ''
        self.passive_order_type = ''
        self.passive_volume_pct = 0
        self.active_maker_cancel_order_time = 0
        self.active_taker_cancel_order_time = 0
        self.passive_maker_cancel_order_time = 0
        self.passive_taker_cancel_order_time = 0
        self.active_passive_cancel_order_pct = 0
        self.active_maker_cancel_order_pct = 0
        self.active_taker_cancel_order_pct = 0
        self.passive_maker_cancel_order_pct = 0
        self.passive_taker_cancel_order_pct = 0
        self.active_maker_fee_rate = 0
        self.active_taker_fee_rate = 0
        self.passive_maker_fee_rate = 0
        self.passive_taker_fee_rate = 0
        self.active_taker_slippage = 0
        self.active_maker_slippage = 0
        self.passive_taker_slippage = 0
        self.passive_maker_slippage = 0
        self.pair_active_total_price = 0
        self.pair_total_volume = 0
        self.pair_passive_total_price = 0
        self.maker_taker_fs = 0
        self.taker_taker_fs = 0
        self.max_mt_order_size = 0
        self.max_tt_order_size = 0
        self.target_spread_type = ''
        self.active_volume_calculate_type = ''
        self.tt_target_volume = 0
        self.mt_target_volume = 0
