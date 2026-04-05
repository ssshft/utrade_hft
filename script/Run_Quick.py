from QuantOrderStatictis import QuantOrderStatictis
from PairOrderStatictis import PairOrderStatictis
import pandas as pd
import signal
import os
from datetime import datetime, date, timedelta
from Object import smc
import numpy as np
import sys
import time
from MysqlManager import MysqlManager

server_name = 'bn_gate'
 
def get_value(instrument_key):
    value = 0
    inst_list = instrument_key.split('.')
    if len(inst_list) >= 3:
        exchange = inst_list[0]
        inst_type = inst_list[1]
        instrument = inst_list[2]
        if 'SPOT' in inst_type:
            value = 1
        else:
            ok, info = smc.get_instrument_info(exchange, inst_type, instrument)
            if ok:
                value = info['value']
    return value

def get_pair_instrument_key(x, y):
    pair_instrument_key = f'{x}|{y}'
    return pair_instrument_key

def calc_total_active_long_real_spread(r):
    total_real_spread = 0
    if r['isActiveOrder'] == 1 and r['direction'] == 'Direction_LONG':
        total_real_spread += r['RealSpread'] * r['activeTotalVolumeOnOrder']
    return total_real_spread

def calc_total_active_long_real_spread_volume(r):
    total_real_spread_volume = 0
    if r['isActiveOrder'] == 1 and r['direction'] == 'Direction_LONG':
        total_real_spread_volume += r['activeTotalVolumeOnOrder']
    return total_real_spread_volume

def calc_total_active_short_real_spread(r):
    total_real_spread = 0
    if r['isActiveOrder'] == 1 and r['direction'] == 'Direction_SHORT':
        total_real_spread += r['RealSpread'] * r['activeTotalVolumeOnOrder']
    return total_real_spread

def calc_total_active_short_real_spread_volume(r):
    total_real_spread_volume = 0
    if r['isActiveOrder'] == 1 and r['direction'] == 'Direction_SHORT':
        total_real_spread_volume += r['activeTotalVolumeOnOrder']
    return total_real_spread_volume

def calc_total_active_slippage(r):
    total_slippage = 0
    if r['isActiveOrder'] == 1:
        total_slippage += r['ActiveSlippage'] * r['totalVolumeOnOrder']
    return total_slippage

def calc_total_active_slippage_volume(r):
    total_volume = 0
    if r['isActiveOrder'] == 1:
        total_volume += r['totalVolumeOnOrder']
    return total_volume

def calc_total_passive_slippage(r):
    total_slippage = 0
    if r['isActiveOrder'] != 1:
        total_slippage += r['PassiveSlippage'] * r['totalVolumeOnOrder']
    return total_slippage

def calc_total_passive_slippage_volume(r):
    total_volume = 0
    if r['isActiveOrder'] != 1:
        total_volume += r['totalVolumeOnOrder']
    return total_volume

def calc_total_open_long_target_spread(r):
    total_spread = 0
    if r['tradingTypeOffset'] == 'OPEN_LONG':
        total_spread += r['TargetSpread'] * r['totalVolumeOnOrder']
    return total_spread

def calc_total_open_long_target_spread_volume(r):
    total_volume = 0
    if r['tradingTypeOffset'] == 'OPEN_LONG':
        total_volume += r['totalVolumeOnOrder']
    return total_volume

def calc_total_open_short_target_spread(r):
    total_spread = 0
    if r['tradingTypeOffset'] == 'OPEN_SHORT':
        total_spread += r['TargetSpread'] * r['totalVolumeOnOrder']
    return total_spread

def calc_total_open_short_target_spread_volume(r):
    total_volume = 0
    if r['tradingTypeOffset'] == 'OPEN_SHORT':
        total_volume += r['totalVolumeOnOrder']
    return total_volume

def calc_total_close_long_target_spread(r):
    total_spread = 0
    if r['tradingTypeOffset'] == 'CLOSE_LONG':
        total_spread += r['TargetSpread'] * r['totalVolumeOnOrder']
    return total_spread

def calc_total_close_long_target_spread_volume(r):
    total_volume = 0
    if r['tradingTypeOffset'] == 'CLOSE_LONG':
        total_volume += r['totalVolumeOnOrder']
    return total_volume

def calc_total_close_short_target_spread_volume(r):
    total_volume = 0
    if r['tradingTypeOffset'] == 'CLOSE_SHORT':
        total_volume += r['totalVolumeOnOrder']
    return total_volume

def calc_total_close_short_target_spread(r):
    total_spread = 0
    if r['tradingTypeOffset'] == 'CLOSE_SHORT':
        total_spread += r['TargetSpread'] * r['totalVolumeOnOrder']
    return total_spread

def calc_total_open_long_real_spread(r):
    total_spread = 0
    if r['tradingTypeOffset'] == 'OPEN_LONG':
        total_spread += r['RealSpread'] * r['totalVolumeOnOrder']
    return total_spread

def calc_total_open_long_real_spread_volume(r):
    total_volume = 0
    if r['tradingTypeOffset'] == 'OPEN_LONG':
        total_volume += r['totalVolumeOnOrder']
    return total_volume

def calc_total_open_short_real_spread(r):
    total_spread = 0
    if r['tradingTypeOffset'] == 'OPEN_SHORT':
        total_spread += r['RealSpread'] * r['totalVolumeOnOrder']
    return total_spread

def calc_total_open_short_real_spread_volume(r):
    total_volume = 0
    if r['tradingTypeOffset'] == 'OPEN_SHORT':
        total_volume += r['totalVolumeOnOrder']
    return total_volume

def calc_total_close_long_real_spread(r):
    total_spread = 0
    if r['tradingTypeOffset'] == 'CLOSE_LONG':
        total_spread += r['RealSpread'] * r['totalVolumeOnOrder']
    return total_spread

def calc_total_close_long_real_spread_volume(r):
    total_volume = 0
    if r['tradingTypeOffset'] == 'CLOSE_LONG':
        total_volume += r['totalVolumeOnOrder']
    return total_volume

def calc_total_close_short_real_spread(r):
    total_spread = 0
    if r['tradingTypeOffset'] == 'CLOSE_SHORT':
        total_spread += r['RealSpread'] * r['totalVolumeOnOrder']
    return total_spread

def calc_total_close_short_real_spread_volume(r):
    total_volume = 0
    if r['tradingTypeOffset'] == 'CLOSE_SHORT':
        total_volume += r['totalVolumeOnOrder']
    return total_volume

def calc_new_order_system_delay(order_time_status):
    pend_new_time = 0
    pending_new_time = 0
    pend_new_to_pending_new_time = 0
    order_time_status_list = order_time_status.split('|')
    for time_status in order_time_status_list:
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

def calc_cancel_order_system_delay(order_time_status):
    cancel_time = 0
    cancelling_time = 0
    cancel_to_cancelling_time = 0
    order_time_status_list = order_time_status.split('|')
    for time_status in order_time_status_list:
        time_status_list = time_status.split('-')
        if len(time_status_list) < 2:
            continue

        t = int(time_status_list[0])
        status = time_status_list[1]

        if status == 'OrderStatus_CANCEL':
            cancel_time = t
        elif status == 'OrderStatus_CANCELLING':
            cancelling_time = t
            break
    if cancel_time > 0 and cancelling_time > 0:
        cancel_to_cancelling_time = (cancelling_time - cancel_time) / 1000
    return cancel_to_cancelling_time

def calc_cancel_order_exchange_delay(order_time_status):
    cancelling_time = 0
    cancelled_time = 0
    cancelling_to_cancelled_time = 0
    order_time_status_list = order_time_status.split('|')
    for time_status in order_time_status_list:
        time_status_list = time_status.split('-')
        if len(time_status_list) < 2:
            continue

        t = int(time_status_list[0])
        status = time_status_list[1]

        if status == 'OrderStatus_CANCELLING':
            cancelling_time = t
        elif status == 'OrderStatus_CANCELLED':
            cancelled_time = t
            break
    if cancelling_time > 0 and cancelled_time > 0:
        cancelling_to_cancelled_time = (cancelled_time - cancelling_time) / 1000
    return cancelling_to_cancelled_time

def calc_new_order_exchange_delay(order_time_status):
    pending_new_time = 0
    exchange_order_time = 0
    pending_new_to_exchange_order_time = 0
    order_time_status_list = order_time_status.split('|')
    for time_status in order_time_status_list:
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
        pending_new_to_exchange_order_time = (exchange_order_time - pending_new_time) / 1000
    return pending_new_to_exchange_order_time

def calc_order_new_to_cancel_delay(order_time_status):
    pend_time = 0
    canecel_time = 0
    canceled_time = 0
    new_to_cancel_time = 0
    order_time_status_list = order_time_status.split('|')
    for time_status in order_time_status_list:
        time_status_list = time_status.split('-')
        if len(time_status_list) < 2:
            continue

        t = int(time_status_list[0])
        status = time_status_list[1]

        if status == 'OrderStatus_PEND_NEW':
            pend_time = t
        elif status == 'OrderStatus_CANCEL':
            canecel_time = t
        elif status == 'OrderStatus_CANCELED':
            canceled_time = t
    if canceled_time > 0:
        if canecel_time > 0 and pend_time > 0:
            new_to_cancel_time = (canecel_time - pend_time) / 1000
    return new_to_cancel_time

def calc_total_active_new_order_system_delay(r):
    total_system_delay = 0
    if r['isActiveOrder'] == 1:
        total_system_delay += r['NewOrderSystemDelay']
    return total_system_delay

def calc_total_active_cancel_order_system_delay(r):
    total_system_delay = 0
    if r['isActiveOrder'] == 1:
        total_system_delay += r['CancelOrderSystemDelay']
    return total_system_delay

def calc_total_active_cancel_order_system_delay_count(r):
    total_system_delay_count = 0
    if r['isActiveOrder'] == 1 and r['CancelOrderSystemDelay'] > 0:
        total_system_delay_count += 1
    return total_system_delay_count

def calc_total_active_order_new_to_cancel_delay(r):
    total_new_to_cancel_delay = 0
    if r['isActiveOrder'] == 1:
        total_new_to_cancel_delay += r['OrderNewToCancelDelay']
    return total_new_to_cancel_delay

def calc_total_active_order_new_to_cancel_delay_count(r):
    total_new_to_cancel_delay_count = 0
    if r['isActiveOrder'] == 1 and r['OrderNewToCancelDelay'] > 0:
        total_new_to_cancel_delay_count += 1
    return total_new_to_cancel_delay_count

def calc_total_active_new_order_exchange_delay(r):
    total_exchange_delay = 0
    if r['isActiveOrder'] == 1:
        total_exchange_delay += r['NewOrderExchangeDelay']
    return total_exchange_delay

def calc_total_passive_new_order_system_delay(r):
    total_system_delay = 0
    if r['isActiveOrder'] != 1:
        total_system_delay += r['NewOrderSystemDelay']
    return total_system_delay

def calc_total_passive_cancel_order_system_delay(r):
    total_system_delay = 0
    if r['isActiveOrder'] != 1:
        total_system_delay += r['CancelOrderSystemDelay']
    return total_system_delay

def calc_total_passive_cancel_order_system_delay_count(r):
    total_system_delay_count = 0
    if r['isActiveOrder'] != 1 and r['CancelOrderSystemDelay'] > 0 :
        total_system_delay_count += 1
    return total_system_delay_count

def calc_total_passive_new_order_exchange_delay(r):
    total_exchange_delay = 0
    if r['isActiveOrder'] != 1:
        total_exchange_delay += r['NewOrderExchangeDelay']
    return total_exchange_delay

def calc_total_active_depth_delay(r):
    total_delay = 0
    if r['pairTotalVolume'] != 0:
        total_delay += r['ActiveDepthDelay']
    return total_delay

def calc_total_passive_depth_delay(r):
    total_delay = 0
    if r['pairTotalVolume'] != 0:
        total_delay += r['PassiveDepthDelay']
    return total_delay

def calc_total_min_active_passive_depth_delay(r):
    total_delay = 0
    if r['pairTotalVolume'] != 0:
        total_delay += r['MinActivePassiveDepthDelay']
    return total_delay

def calc_total_generate_delay(r):
    total_delay = 0
    if r['pairTotalVolume'] != 0:
        total_delay += r['GenerateDelay']
    return total_delay

def calc_total_pair_volume_count(r):
    total_count = 0
    if r['pairTotalVolume'] != 0:
        total_count += 1
    return total_count

def calc_values(df):
    s1 = df['ActiveTotalTradeAmount'].sum()
    s2 = df['PassiveTotalTradeAmount'].sum()
    s3 = df['ActiveTotalOrderAmount'].sum()
    s4 = df['PassiveTotalOrderAmount'].sum()
    s5 = df['ActiveTotalTradeOrderCount'].sum()
    s6 = df['PassiveTotalTradeOrderCount'].sum()
    s7 = df['ActiveTotalOrderCount'].sum()
    s8 = df['PassiveTotalOrderCount'].sum()
    s9 = 0
    if s7 > 0:
        s9 = s5 / s7 # ActiveTradeRate
    s10 = 0
    if s8 > 0:
        s10 = s6 / s8 # PassiveTradeRate


    df['TotalActiveLongRealSpread'] = df.apply(calc_total_active_long_real_spread, axis=1)
    df['TotalActiveLongRealSpreadVolume'] = df.apply(calc_total_active_long_real_spread_volume, axis=1)
    total_active_long_real_spread = df['TotalActiveLongRealSpread'].sum()
    total_active_long_real_spread_volume = df['TotalActiveLongRealSpreadVolume'].sum()
    s11 = 0
    if total_active_long_real_spread_volume != 0:
        s11 = total_active_long_real_spread / total_active_long_real_spread_volume # active_long_real_spread

    df['TotalActiveShortRealSpread'] = df.apply(calc_total_active_short_real_spread, axis=1)
    df['TotalActiveShortRealSpreadVolume'] = df.apply(calc_total_active_short_real_spread_volume, axis=1)
    total_active_short_real_spread = df['TotalActiveShortRealSpread'].sum()
    total_active_short_real_spread_volume = df['TotalActiveShortRealSpreadVolume'].sum()
    s12 = 0
    if total_active_short_real_spread_volume != 0:
        s12 = total_active_short_real_spread / total_active_short_real_spread_volume # active_short_real_spread

    df['TotalActiveSlippage'] = df.apply(calc_total_active_slippage, axis=1)
    df['TotalActiveSlippageVolume'] = df.apply(calc_total_active_slippage_volume, axis=1)
    total_active_slippage = df['TotalActiveSlippage'].sum()
    total_active_slippage_volume = df['TotalActiveSlippageVolume'].sum()
    s13 = 0
    if total_active_slippage_volume != 0:
        s13 = total_active_slippage / total_active_slippage_volume # total_active_slippage

    df['TotalPassiveSlippage'] = df.apply(calc_total_passive_slippage, axis=1)
    df['TotalPassiveSlippageVolume'] = df.apply(calc_total_passive_slippage_volume, axis=1)
    total_passive_slippage = df['TotalPassiveSlippage'].sum()
    total_passive_slippage_volume = df['TotalPassiveSlippageVolume'].sum()
    s14 = 0
    if total_passive_slippage_volume != 0:
        s14 = total_passive_slippage / total_passive_slippage_volume # total_passive_slippage

    total_active_long_trade_amount = df['TotalActiveLongTradeAmount'].sum()
    total_active_long_trade_volume = df['TotalActiveLongTradeVolume'].sum()
    s15 = total_active_long_trade_amount # total_active_long_trade_amount
    s16 = 0
    if total_active_long_trade_volume > 0:
        s16 = total_active_long_trade_amount / total_active_long_trade_volume # total_active_long_trade_price

    total_active_short_trade_amount = df['TotalActiveShortTradeAmount'].sum()
    total_active_short_trade_volume = df['TotalActiveShortTradeVolume'].sum()
    s17 = total_active_short_trade_amount # total_active_short_trade_amount
    s18 = 0
    if total_active_short_trade_volume > 0:
        s18 = total_active_short_trade_amount / total_active_short_trade_volume # total_active_short_trade_price

    total_passive_long_trade_amount = df['TotalPassiveLongTradeAmount'].sum()
    total_passive_long_trade_volume = df['TotalPassiveLongTradeVolume'].sum()
    s19 = total_passive_long_trade_amount # total_passive_long_trade_amount
    s20 = 0
    if total_passive_long_trade_volume > 0:
        s20 = total_passive_long_trade_amount / total_passive_long_trade_volume # total_passive_long_trade_price

    total_passive_short_trade_amount = df['TotalPassiveShortTradeAmount'].sum()
    total_passive_short_trade_volume = df['TotalPassiveShortTradeVolume'].sum()
    s21 = total_passive_short_trade_amount # total_passive_short_trade_amount
    s22 = 0
    if total_passive_short_trade_volume > 0:
        s22 = total_passive_short_trade_amount / total_passive_short_trade_volume # total_passive_short_trade_price

    no_repeat_df = df.drop_duplicates(subset=['pairId'], keep='last').copy()
    no_repeat_df['TotalOpenLongTargetSpread'] = no_repeat_df.apply(calc_total_open_long_target_spread, axis=1)
    no_repeat_df['TotalOpenLongTargetSpreadVolume'] = no_repeat_df.apply(calc_total_open_long_target_spread_volume, axis=1)
    total_open_long_target_spread = no_repeat_df['TotalOpenLongTargetSpread'].sum()
    total_open_long_target_spread_volume = no_repeat_df['TotalOpenLongTargetSpreadVolume'].sum()
    s23 = 0
    if total_open_long_target_spread_volume != 0:
        s23 = total_open_long_target_spread / total_open_long_target_spread_volume # total_open_long_target_spread

    no_repeat_df['TotalOpenShortTargetSpread'] = no_repeat_df.apply(calc_total_open_short_target_spread, axis=1)
    no_repeat_df['TotalOpenShortTargetSpreadVolume'] = no_repeat_df.apply(calc_total_open_short_target_spread_volume, axis=1)
    total_open_short_target_spread = no_repeat_df['TotalOpenShortTargetSpread'].sum()
    total_open_short_target_spread_volume = no_repeat_df['TotalOpenShortTargetSpreadVolume'].sum()
    s24 = 0
    if total_open_short_target_spread_volume != 0:
        s24 = total_open_short_target_spread / total_open_short_target_spread_volume # total_open_short_target_spread

    no_repeat_df['TotalCloseLongTargetSpread'] = no_repeat_df.apply(calc_total_close_long_target_spread, axis=1)
    no_repeat_df['TotalCloseLongTargetSpreadVolume'] = no_repeat_df.apply(calc_total_close_long_target_spread_volume, axis=1)
    total_close_long_target_spread = no_repeat_df['TotalCloseLongTargetSpread'].sum()
    total_close_long_target_spread_volume = no_repeat_df['TotalCloseLongTargetSpreadVolume'].sum()
    s25 = 0
    if total_close_long_target_spread_volume != 0:
        s25 = total_close_long_target_spread / total_close_long_target_spread_volume # total_close_long_target_spread

    no_repeat_df['TotalCloseShortTargetSpread'] = no_repeat_df.apply(calc_total_close_short_target_spread, axis=1)
    no_repeat_df['TotalCloseShortTargetSpreadVolume'] = no_repeat_df.apply(calc_total_close_short_target_spread_volume, axis=1)
    total_close_short_target_spread = no_repeat_df['TotalCloseShortTargetSpread'].sum()
    total_close_short_target_spread_volume = no_repeat_df['TotalCloseShortTargetSpreadVolume'].sum()
    s26 = 0
    if total_close_short_target_spread_volume != 0:
        s26 = total_close_short_target_spread / total_close_short_target_spread_volume # total_close_short_target_spread

    no_repeat_df['TotalOpenLongRealSpread'] = no_repeat_df.apply(calc_total_open_long_real_spread, axis=1)
    no_repeat_df['TotalOpenLongRealSpreadVolume'] = no_repeat_df.apply(calc_total_open_long_real_spread_volume, axis=1)
    total_open_long_real_spread = no_repeat_df['TotalOpenLongRealSpread'].sum()
    total_open_long_real_spread_volume = no_repeat_df['TotalOpenLongRealSpreadVolume'].sum()
    s27 = 0
    if total_open_long_real_spread_volume != 0:
        s27 = total_open_long_real_spread / total_open_long_real_spread_volume # total_open_long_real_spread

    no_repeat_df['TotalOpenShortRealSpread'] = no_repeat_df.apply(calc_total_open_short_real_spread, axis=1)
    no_repeat_df['TotalOpenShortRealSpreadVolume'] = no_repeat_df.apply(calc_total_open_short_real_spread_volume, axis=1)
    total_open_short_real_spread = no_repeat_df['TotalOpenShortRealSpread'].sum()
    total_open_short_real_spread_volume = no_repeat_df['TotalOpenShortRealSpreadVolume'].sum()
    s28 = 0
    if total_open_short_real_spread_volume != 0:
        s28 = total_open_short_real_spread / total_open_short_real_spread_volume # total_open_short_real_spread

    no_repeat_df['TotalCloseLongRealSpread'] = no_repeat_df.apply(calc_total_close_long_real_spread, axis=1)
    no_repeat_df['TotalCloseLongRealSpreadVolume'] = no_repeat_df.apply(calc_total_close_long_real_spread_volume, axis=1)
    total_close_long_real_spread = no_repeat_df['TotalCloseLongRealSpread'].sum()
    total_close_long_real_spread_volume = no_repeat_df['TotalCloseLongRealSpreadVolume'].sum()
    s29 = 0
    if total_close_long_real_spread_volume != 0:
        s29 = total_close_long_real_spread / total_close_long_real_spread_volume # total_close_long_real_spread

    no_repeat_df['TotalCloseShortRealSpread'] = no_repeat_df.apply(calc_total_close_short_real_spread, axis=1)
    no_repeat_df['TotalCloseShortRealSpreadVolume'] = no_repeat_df.apply(calc_total_close_short_real_spread_volume, axis=1)
    total_close_short_real_spread = no_repeat_df['TotalCloseShortRealSpread'].sum()
    total_close_short_real_spread_volume = no_repeat_df['TotalCloseShortRealSpreadVolume'].sum()
    s30 = 0
    if total_close_short_real_spread_volume != 0:
        s30 = total_close_short_real_spread / total_close_short_real_spread_volume # total_close_short_real_spread

    df['TotalActiveNewOrderSystemDelay'] = df.apply(calc_total_active_new_order_system_delay, axis=1)
    total_active_new_order_system_delay = df['TotalActiveNewOrderSystemDelay'].sum()
    total_active_order_count = df['ActiveTotalOrderCount'].sum()
    s31 = 0
    if total_active_order_count != 0:
        s31 = total_active_new_order_system_delay / total_active_order_count # total_active_new_order_system_delay

    df['TotalActiveCancelOrderSystemDelay'] = df.apply(calc_total_active_cancel_order_system_delay, axis=1)
    df['TotalActiveCancelOrderSystemDelayCount'] = df.apply(calc_total_active_cancel_order_system_delay_count, axis=1)
    total_active_cancel_order_system_delay = df['TotalActiveCancelOrderSystemDelay'].sum()
    total_active_cancel_order_system_delay_count = df['TotalActiveCancelOrderSystemDelayCount'].sum()
    s32 = 0
    if total_active_order_count != 0:
        s32 = total_active_cancel_order_system_delay / total_active_cancel_order_system_delay_count # total_active_cancel_order_system_delay

    df['TotalActiveNewOrderExchangeDelay'] = df.apply(calc_total_active_new_order_exchange_delay, axis=1)
    total_active_new_order_exchange_delay = df['TotalActiveNewOrderExchangeDelay'].sum()
    #total_active_order_count = df['ActiveTotalOrderCount'].sum()
    s33 = 0
    if total_active_order_count != 0:
        s33 = total_active_new_order_exchange_delay / total_active_order_count # total_active_new_order_exchange_delay

    df['TotalPassiveNewOrderSystemDelay'] = df.apply(calc_total_passive_new_order_system_delay, axis=1)
    total_passive_new_order_system_delay = df['TotalPassiveNewOrderSystemDelay'].sum()
    total_passive_order_count = df['PassiveTotalOrderCount'].sum()
    s34 = 0
    if total_passive_order_count != 0:
        s34 = total_passive_new_order_system_delay / total_passive_order_count # total_passive_new_order_system_delay

    df['TotalPassiveCancelOrderSystemDelay'] = df.apply(calc_total_passive_cancel_order_system_delay, axis=1)
    df['TotalPassiveCancelOrderSystemDelayCount'] = df.apply(calc_total_passive_cancel_order_system_delay_count, axis=1)
    total_passive_cancel_order_system_delay = df['TotalPassiveCancelOrderSystemDelay'].sum()
    total_passive_cancel_order_system_delay_count = df['TotalPassiveCancelOrderSystemDelayCount'].sum()
    s35 = 0
    if total_passive_cancel_order_system_delay_count != 0:
        s35 = total_passive_cancel_order_system_delay / total_passive_cancel_order_system_delay_count # total_passive_cancel_order_system_delay

    df['TotalPassiveNewOrderExchangeDelay'] = df.apply(calc_total_passive_new_order_exchange_delay, axis=1)
    total_passive_new_order_exchange_delay = df['TotalPassiveNewOrderExchangeDelay'].sum()
    #total_passive_order_count = df['PassiveTotalOrderCount'].sum()
    s36 = 0
    if total_passive_order_count != 0:
        s36 = total_passive_new_order_exchange_delay / total_passive_order_count # total_passive_new_order_exchange_delay

    no_repeat_df['TotalActiveDepthDelay'] = no_repeat_df.apply(calc_total_active_depth_delay, axis=1)
    no_repeat_df['TotalPassiveDepthDelay'] = no_repeat_df.apply(calc_total_passive_depth_delay, axis=1)
    no_repeat_df['TotalMinActivePassiveDepthDelay'] = no_repeat_df.apply(calc_total_min_active_passive_depth_delay, axis=1)
    no_repeat_df['TotalGenerateDelay'] = no_repeat_df.apply(calc_total_generate_delay, axis=1)
    no_repeat_df['TotalPairVolumeCount'] = no_repeat_df.apply(calc_total_pair_volume_count, axis=1)

    total_active_depth_delay = no_repeat_df['TotalActiveDepthDelay'].sum()
    total_passive_depth_delay = no_repeat_df['TotalPassiveDepthDelay'].sum()
    total_min_active_passive_depth_delay = no_repeat_df['TotalMinActivePassiveDepthDelay'].sum()
    total_generate_delay = no_repeat_df['TotalGenerateDelay'].sum()
    total_pair_volume_count = no_repeat_df['TotalPairVolumeCount'].sum()

    s37 = 0
    s38 = 0
    s39 = 0
    s40 = 0
    if total_pair_volume_count > 0:
        s37 = total_active_depth_delay / total_pair_volume_count # avg_active_depth_delay
        s38 = total_passive_depth_delay / total_pair_volume_count # avg_passive_depth_delay
        s39 = total_min_active_passive_depth_delay / total_pair_volume_count # avg_min_active_passive_depth_delay
        s40 = total_generate_delay / total_pair_volume_count # avg_generate_delay

    s41 = no_repeat_df['pairTotalVolume'].iloc[-1] # pair_total_volume
    s42 = no_repeat_df['pairActiveTotalPrice'].iloc[-1] # pair_active_total_price
    s43 = no_repeat_df['pairPassiveTotalPrice'].iloc[-1] # pair_passive_total_price

    df['TotalActiveOrderNewToCancelDelay'] = df.apply(calc_total_active_order_new_to_cancel_delay, axis=1)
    df['TotalActiveOrderNewToCancelDelayCount'] = df.apply(calc_total_active_order_new_to_cancel_delay_count, axis=1)
    total_active_order_new_to_cancel_delay = df['TotalActiveOrderNewToCancelDelay'].sum()
    total_active_order_new_to_cancel_delay_count = df['TotalActiveOrderNewToCancelDelayCount'].sum()
    s44 = 0
    if total_active_order_new_to_cancel_delay_count != 0:
        s44 = total_active_order_new_to_cancel_delay / total_active_order_new_to_cancel_delay_count # total_order_new_to_cancel_delay

    return [s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15, s16, s17, s18, s19, s20, s21, s22, s23, s24, s25, s26, s27, s28, s29, s30, s31, s32, s33, s34, s35, s36, s37, s38, s39, s40, s41, s42, s43, s44]


def generate_statistic(run_path, date_str, name=''):
    start_time = datetime.now()
    #run_path = './run'
    #run_path = './run_bybit'
    #date_str = '2023-07-11'
    print(name)

    if name:
        pair_order_path = f'{run_path}/{date_str}_{name}pairOrder.csv'
        quant_order_path = f'{run_path}/{date_str}_{name}quantOrder.csv'
    else:
        pair_order_path = f'{run_path}/{date_str}_pairOrder.csv'
        quant_order_path = f'{run_path}/{date_str}_quantOrder.csv'
    print(pair_order_path)
    print(quant_order_path)

    #pair_order_path = f'{run_path}/all_pairOrder.csv'
    #quant_order_path = f'{run_path}/all_quantOrder.csv'

    df_pair_order = pd.read_csv(pair_order_path)
    df_last_pair_order = df_pair_order.drop_duplicates(subset=['pairId'], keep='last').copy()
    df_first_pair_order = df_pair_order.drop_duplicates(subset=['pairId'], keep='first').copy()

    pair_order_columns = list(df_last_pair_order.columns)
    pair_order_columns.remove('generateTs')
    pair_order_columns.remove('activeDepthTs')
    pair_order_columns.remove('passiveDepthTs')
    pair_order_columns.remove('createTime')
    pair_order_columns.remove('pairId')
    ts_l = ['generateTs', 'activeDepthTs', 'passiveDepthTs', 'createTime']

    df_first_pair_order.drop(pair_order_columns, axis=1, inplace=True)
    df_last_pair_order.drop(ts_l, axis=1, inplace=True)
    
    df_last_pair_order = pd.merge(df_last_pair_order, df_first_pair_order, on='pairId', how='left')
    df_last_pair_order['PairInstrumentKey'] = df_last_pair_order['activeInstrumentKey'] + "|" + df_last_pair_order['passiveInstrumentKey']
    df_last_pair_order['RealSpread'] = df_last_pair_order.apply(lambda x: x['passiveTotalPriceOnOrder'] / x['activeTotalPriceOnOrder'] - 1 if x['activeTotalPriceOnOrder'] > 0 and x['passiveTotalPriceOnOrder'] > 0 else x['passiveTargetPrice'] / x['activeTotalPriceOnOrder'] - 1 if x['activeTotalPriceOnOrder'] > 0 and x['passiveTotalPriceOnOrder'] <= 0 else 0, axis=1)

    df_last_pair_order['TargetSpread'] = df_last_pair_order.apply(lambda x: 0 if x['activeTargetPrice'] == 0 else x['passiveTargetPrice'] / x['activeTargetPrice'] - 1, axis=1)

    df_last_pair_order['ActiveDepthDelay'] = df_last_pair_order.apply(lambda x: (x['createTime'] - x['activeDepthTs']) / 1000, axis=1)
    df_last_pair_order['PassiveDepthDelay'] = df_last_pair_order.apply(lambda x: (x['createTime'] - x['passiveDepthTs']) / 1000, axis=1)
    df_last_pair_order['MinActivePassiveDepthDelay'] = df_last_pair_order.apply(lambda x: min(x['ActiveDepthDelay'], x['PassiveDepthDelay']), axis=1)
    df_last_pair_order['GenerateDelay'] = df_last_pair_order.apply(lambda x: (x['createTime'] - x['generateTs']) / 1000, axis=1)


    df_quant_order = pd.read_csv(quant_order_path)
    df_last_quant_order = df_quant_order.drop_duplicates(subset=['strategyOrderId'], keep='last').copy()
    df_last_quant_order.rename(columns={'updateTime': 'quantOrderUpdateTime', 'strategyName': 'quantOrderStrategyName'}, inplace=True)
    df_last_quant_order['value'] = df_last_quant_order['instrumentKey'].map(get_value)

    df_last_quant_order['TotalTradeAmount'] = df_last_quant_order['totalPriceOnOrder'] * df_last_quant_order['totalVolumeOnOrder'] * df_last_quant_order['value']
    df_last_quant_order['ActiveTotalTradeAmount'] = df_last_quant_order['TotalTradeAmount'] * df_last_quant_order['isActiveOrder']
    df_last_quant_order['PassiveTotalTradeAmount'] = df_last_quant_order['TotalTradeAmount'] - df_last_quant_order['ActiveTotalTradeAmount']

    df_last_quant_order['TotalOrderAmount'] = df_last_quant_order['price'] * df_last_quant_order['volume'] * df_last_quant_order['value']
    df_last_quant_order['ActiveTotalOrderAmount'] = df_last_quant_order['TotalOrderAmount'] * df_last_quant_order['isActiveOrder']
    df_last_quant_order['PassiveTotalOrderAmount'] = df_last_quant_order['TotalOrderAmount'] - df_last_quant_order['ActiveTotalOrderAmount']

    df_last_quant_order['TotalTradeOrderCount'] = df_last_quant_order['totalVolumeOnOrder'].map(lambda x: 1 if abs(x) > 0 else 0)
    df_last_quant_order['ActiveTotalTradeOrderCount'] = df_last_quant_order['TotalTradeOrderCount'] * df_last_quant_order['isActiveOrder']
    df_last_quant_order['PassiveTotalTradeOrderCount'] = df_last_quant_order['TotalTradeOrderCount'] - df_last_quant_order['ActiveTotalTradeOrderCount']


    df_last_quant_order['TotalOrderCount'] = 1
    df_last_quant_order['ActiveTotalOrderCount'] = df_last_quant_order['TotalOrderCount'] * df_last_quant_order['isActiveOrder']
    df_last_quant_order['PassiveTotalOrderCount'] = df_last_quant_order['TotalOrderCount'] - df_last_quant_order['ActiveTotalOrderCount']

    df_last_quant_order['ActiveSlippage'] = df_last_quant_order.apply(lambda x: x['totalPriceOnOrder'] / x['targetPrice'] - 1 if x['isActiveOrder'] == 1 and x['totalPriceOnOrder'] > 0 and x['direction'] == 'Direction_LONG' else x['targetPrice'] / x['totalPriceOnOrder'] - 1 if x['isActiveOrder'] == 1 and x['totalPriceOnOrder'] > 0 and x['direction'] == 'Direction_SHORT' else 0, axis=1)
    df_last_quant_order['PassiveSlippage'] = df_last_quant_order.apply(lambda x: x['totalPriceOnOrder'] / x['targetPrice'] - 1 if x['isActiveOrder'] != 1 and x['totalPriceOnOrder'] > 0 and x['direction'] == 'Direction_LONG' else x['targetPrice'] / x['totalPriceOnOrder'] - 1 if x['isActiveOrder'] != 1 and x['totalPriceOnOrder'] > 0 and x['direction'] == 'Direction_SHORT' else 0, axis=1)


    df_last_quant_order['TotalActiveLongTradeAmount'] = df_last_quant_order.apply(lambda x: x['TotalTradeAmount'] if x['isActiveOrder'] == 1 and x['direction'] == 'Direction_LONG' else 0, axis=1)
    df_last_quant_order['TotalActiveLongTradeVolume'] = df_last_quant_order.apply(lambda x: x['totalVolumeOnOrder'] if x['isActiveOrder'] == 1 and x['direction'] == 'Direction_LONG' else 0, axis=1)

    df_last_quant_order['TotalActiveShortTradeAmount'] = df_last_quant_order.apply(lambda x: x['TotalTradeAmount'] if x['isActiveOrder'] == 1 and x['direction'] == 'Direction_SHORT' else 0, axis=1)
    df_last_quant_order['TotalActiveShortTradeVolume'] = df_last_quant_order.apply(lambda x: x['totalVolumeOnOrder'] if x['isActiveOrder'] == 1 and x['direction'] == 'Direction_SHORT' else 0, axis=1)

    df_last_quant_order['TotalPassiveLongTradeAmount'] = df_last_quant_order.apply(lambda x: x['TotalTradeAmount'] if x['isActiveOrder'] != 1 and x['direction'] == 'Direction_LONG' else 0, axis=1)
    df_last_quant_order['TotalPassiveLongTradeVolume'] = df_last_quant_order.apply(lambda x: x['totalVolumeOnOrder'] if x['isActiveOrder'] != 1 and x['direction'] == 'Direction_LONG' else 0, axis=1)

    df_last_quant_order['TotalPassiveShortTradeAmount'] = df_last_quant_order.apply(lambda x: x['TotalTradeAmount'] if x['isActiveOrder'] != 1 and x['direction'] == 'Direction_SHORT' else 0, axis=1)
    df_last_quant_order['TotalPassiveShortTradeVolume'] = df_last_quant_order.apply(lambda x: x['totalVolumeOnOrder'] if x['isActiveOrder'] != 1 and x['direction'] == 'Direction_SHORT' else 0, axis=1)

    df_last_quant_order['NewOrderSystemDelay'] = df_last_quant_order['orderTimeStatus'].map(calc_new_order_system_delay)
    df_last_quant_order['CancelOrderSystemDelay'] = df_last_quant_order['orderTimeStatus'].map(calc_cancel_order_system_delay)
    df_last_quant_order['CancelOrderExchangeDelay'] = df_last_quant_order['orderTimeStatus'].map(calc_cancel_order_exchange_delay)
    df_last_quant_order['NewOrderExchangeDelay'] = df_last_quant_order['orderTimeStatus'].map(calc_new_order_exchange_delay)
    df_last_quant_order['OrderNewToCancelDelay'] = df_last_quant_order['orderTimeStatus'].map(calc_order_new_to_cancel_delay)

    if name == '':
        exchange_id_list = []
        m_new_order_system_delay_count = {}
        m_total_new_order_system_delay = {}
        m_cancel_order_system_delay_count = {}
        m_total_cancel_order_system_delay = {}
        m_cancel_order_exchange_delay_count = {}
        m_total_cancel_order_exchange_delay = {}
        m_new_order_exchange_delay_count = {}
        m_total_new_order_exchange_delay = {}
        for key, value in df_last_quant_order.iterrows():
            inst_key = value['instrumentKey']
            exchange_id = inst_key.split('.')[0]

            new_order_system_delay = value['NewOrderSystemDelay']
            if new_order_system_delay > 0:
                if exchange_id not in m_new_order_system_delay_count.keys():
                    m_new_order_system_delay_count[exchange_id] = 1
                else:
                    m_new_order_system_delay_count[exchange_id] += 1

                if exchange_id not in m_total_new_order_system_delay.keys():
                    m_total_new_order_system_delay[exchange_id] = new_order_system_delay
                else:
                    m_total_new_order_system_delay[exchange_id] += new_order_system_delay

            cancel_order_system_delay = value['CancelOrderSystemDelay']
            if cancel_order_system_delay > 0:
                if exchange_id not in m_cancel_order_system_delay_count.keys():
                    m_cancel_order_system_delay_count[exchange_id] = 1
                else:
                    m_cancel_order_system_delay_count[exchange_id] += 1

                if exchange_id not in m_total_cancel_order_system_delay.keys():
                    m_total_cancel_order_system_delay[exchange_id] = cancel_order_system_delay
                else:
                    m_total_cancel_order_system_delay[exchange_id] += cancel_order_system_delay

            new_order_exchange_delay = value['NewOrderExchangeDelay']
            if new_order_exchange_delay > 0:
                if exchange_id not in m_new_order_exchange_delay_count.keys():
                    m_new_order_exchange_delay_count[exchange_id] = 1
                else:
                    m_new_order_exchange_delay_count[exchange_id] += 1

                if exchange_id not in m_total_new_order_exchange_delay.keys():
                    m_total_new_order_exchange_delay[exchange_id] = new_order_exchange_delay
                else:
                    m_total_new_order_exchange_delay[exchange_id] += new_order_exchange_delay

            cancel_order_exchange_delay = value['CancelOrderExchangeDelay']
            if cancel_order_exchange_delay > 0:
                if exchange_id not in m_cancel_order_exchange_delay_count.keys():
                    m_cancel_order_exchange_delay_count[exchange_id] = 1
                else:
                    m_cancel_order_exchange_delay_count[exchange_id] += 1

                if exchange_id not in m_total_cancel_order_exchange_delay.keys():
                    m_total_cancel_order_exchange_delay[exchange_id] = cancel_order_exchange_delay
                else:
                    m_total_cancel_order_exchange_delay[exchange_id] += cancel_order_exchange_delay

        exchange_id_list.extend(m_total_new_order_system_delay.keys())
        exchange_id_list.extend(m_total_cancel_order_system_delay.keys())
        exchange_id_list.extend(m_total_new_order_exchange_delay.keys())
        exchange_id_list.extend(m_total_cancel_order_exchange_delay.keys())
        exchange_id_list = list(set(exchange_id_list))

        data = []
        for exchange_id in exchange_id_list:
            total_new_order_system_delay = m_total_new_order_system_delay.get(exchange_id, 0)
            new_order_system_delay_count = m_new_order_system_delay_count.get(exchange_id, 0)
            avg_new_order_system_delay = 0
            if new_order_system_delay_count > 0:
                avg_new_order_system_delay = total_new_order_system_delay / new_order_system_delay_count

            total_cancel_order_system_delay = m_total_cancel_order_system_delay.get(exchange_id, 0)
            cancel_order_system_delay_count = m_cancel_order_system_delay_count.get(exchange_id, 0)
            avg_cancel_order_system_delay = 0
            if cancel_order_system_delay_count > 0:
                avg_cancel_order_system_delay = total_cancel_order_system_delay / cancel_order_system_delay_count

            total_new_order_exchange_delay = m_total_new_order_exchange_delay.get(exchange_id, 0)
            new_order_exchange_delay_count = m_new_order_exchange_delay_count.get(exchange_id, 0)
            avg_new_order_exchange_delay = 0
            if new_order_exchange_delay_count > 0:
                avg_new_order_exchange_delay = total_new_order_exchange_delay / new_order_exchange_delay_count

            total_cancel_order_exchange_delay = m_total_cancel_order_exchange_delay.get(exchange_id, 0)
            cancel_order_exchange_delay_count = m_cancel_order_exchange_delay_count.get(exchange_id, 0)
            avg_cancel_order_exchange_delay = 0
            if cancel_order_exchange_delay_count > 0:
                avg_cancel_order_exchange_delay = total_cancel_order_exchange_delay / cancel_order_exchange_delay_count

            d = [exchange_id, avg_new_order_system_delay, avg_cancel_order_system_delay, avg_new_order_exchange_delay, avg_cancel_order_exchange_delay, date_str, server_name]
            data.append(d)

            sql_manager = MysqlManager('172.31.36.194', 3306, 'root', 'Tr8EWXcvSjVwDeW%', 'rad_bp_se')
            sql_manager.insert_trade_delay_data(data)



    df_last_pair_order = pd.merge(df_last_quant_order, df_last_pair_order, on=['pairId', 'algoPairId'], how='left')

    d1 = df_last_pair_order.groupby('PairInstrumentKey').apply(calc_values)
    dict_data = d1.to_dict()

    columns_quant_order = ['ActiveTotalTradeAmount', 'PassiveTotalTradeAmount', 'ActiveTotalOrderAmount', 'PassiveTotalOrderAmount', 'ActiveTotalTradeOrderCount', 'PassiveTotalTradeOrderCount', 'ActiveTotalOrderCount', 'PassiveTotalOrderCount', 'ActiveTradeRate', 'PassiveTradeRate', 'ActiveLongRealSpread', 'ActiveShortRealSpread', 'TotalActiveSlippage', 'TotalPassiveSlippage', 'TotalActiveLongTradeAmount', 'TotalActiveLongTradePrice', 'TotalActiveShortTradeAmount', 'TotalActiveShortTradePrice', 'TotalPassiveLongTradeAmount', 'TotalPassiveLongTradePrice', 'TotalPassiveShortTradeAmount', 'TotalPassiveShortTradePrice']
    columns_pair_order = ['OpenLongTargetSpread', 'OpenShortTargetSpread', 'CloseLongTargetSpread', 'CloseShortTargetSpread', 'OpenLongRealSpread', 'OpenShortRealSpread', 'CloseLongRealSpread', 'CloseShortRealSpread', 'AvgActiveNewOrderSystemDelay', 'AvgActiveCancelOrderSystemDelay', 'AvgActiveNewOrderExchangeDelay', 'AvgPassiveNewOrderSystemDelay', 'AvgPassiveCancelOrderSystemDelay', 'AvgPassiveNewOrderExchangeDelay', 'AvgActiveDepthDelay', 'AvgPassiveDepthDelay', 'AvgMinActivePassiveDepthDelay', 'AvgActiveGenerateDelay', 'PairTotalVolume', 'PairActiveTotalPrice', 'PairPassiveTotalPrice', 'AvgActiveOrderNewToCancelDelay']
    
    columns = []
    columns.extend(columns_quant_order)
    columns.extend(columns_pair_order)

    df = pd.DataFrame.from_dict(dict_data, orient='index', columns=columns)
    df.reset_index(inplace=True)
    df.rename(columns={'index': 'PairInstrumentKey'}, inplace=True)
    df.set_index('PairInstrumentKey', inplace=True)
    df = df.sort_values(by='ActiveTotalTradeAmount', ascending=False)

    if name:
        df.to_csv(f'{run_path}/Statictis_{date_str}_{name}.csv')
    else:
        df.to_csv(f'{run_path}/Statictis_{date_str}.csv')

    #df.to_csv(f'{run_path}/Statictis_all.csv')

    end_time = datetime.now()
    total_time = end_time - start_time
    print(f'calc time: {total_time}')

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('Parameter error!')
        sys.exit(0)

    run_path = sys.argv[1]
    last_date = date.today() + timedelta(days=-1)
    date_str = f'{last_date}'
    for file_name in os.listdir(run_path):
        if date_str in file_name and 'pairOrder' in file_name:
            str_name = file_name.strip(f'{date_str}_')
            str_name = str_name.rstrip("pairOrder.csv")

            generate_statistic(run_path, date_str, str_name)
            time.sleep(1)
    os.kill(os.getpid(), signal.SIGKILL)
