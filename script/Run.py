from QuantOrderStatictis import QuantOrderStatictis
from PairOrderStatictis import PairOrderStatictis
import pandas as pd
import signal
import os
from datetime import datetime
from Object import smc
 

if __name__ == '__main__':
    run_path = '../'
    #run_path = './run_bybit'
    date_str = '2023-08-15'
    start_time = datetime.now()
    pair_order_statictis = PairOrderStatictis()
    pair_order_statictis.load_file(f'./{run_path}/{date_str}_pairOrder.csv', f'./{run_path}/{date_str}_quantOrder.csv')
    #pair_order_statictis.load_file('./all_pairOrder.csv', './all_quantOrder.csv')

    # pair_order_statictis.calc_active_total_trade_amount()
    # pair_order_statictis.calc_passive_total_trade_amount()
    # pair_order_statictis.calc_active_total_order_amount()
    # pair_order_statictis.calc_passive_total_order_amount()
    # pair_order_statictis.calc_active_trade_order_count()
    # pair_order_statictis.calc_passive_trade_order_count()
    # pair_order_statictis.calc_active_order_count()
    # pair_order_statictis.calc_passive_order_count()
    # pair_order_statictis.calc_active_trade_rate()
    # pair_order_statictis.calc_passive_trade_rate()
    # pair_order_statictis.calc_active_long_trade_amount()
    # pair_order_statictis.calc_active_long_trade_volume()
    # pair_order_statictis.calc_active_long_trade_price()
    # pair_order_statictis.calc_active_short_trade_amount()
    # pair_order_statictis.calc_active_short_trade_volume()
    # pair_order_statictis.calc_active_short_trade_price()
    # pair_order_statictis.calc_passive_long_trade_amount()
    # pair_order_statictis.calc_passive_long_trade_volume()
    # pair_order_statictis.calc_passive_long_trade_price()
    # pair_order_statictis.calc_passive_short_trade_amount()
    # pair_order_statictis.calc_passive_short_trade_volume()
    # pair_order_statictis.calc_passive_short_trade_price()
    # pair_order_statictis.calc_active_slippage()
    # pair_order_statictis.calc_passive_slippage()
    # pair_order_statictis.calc_active_new_order_system_delay()
    # pair_order_statictis.calc_active_cancel_order_system_delay()
    # pair_order_statictis.calc_active_new_order_exchange_delay()
    # pair_order_statictis.calc_passive_new_order_system_delay()
    # pair_order_statictis.calc_passive_cancel_order_system_delay()
    # pair_order_statictis.calc_passive_new_order_exchange_delay()
    # pair_order_statictis.calc_total_open_long_target_spread()
    # pair_order_statictis.calc_total_open_short_target_spread()
    # pair_order_statictis.calc_total_close_long_target_spread()
    # pair_order_statictis.calc_total_close_short_target_spread()
    # pair_order_statictis.calc_total_open_long_real_spread()
    # pair_order_statictis.calc_total_open_short_real_spread()
    # pair_order_statictis.calc_total_close_long_real_spread()
    # pair_order_statictis.calc_total_close_short_real_spread()
    # pair_order_statictis.calc_active_long_real_spread()
    # pair_order_statictis.calc_active_short_real_spread()
    # pair_order_statictis.calc_active_depth_delay()
    # pair_order_statictis.calc_passive_depth_delay()
    # pair_order_statictis.calc_min_active_passive_depth_delay()
    # pair_order_statictis.calc_active_generate_delay()

    pair_order_statictis.calc_all()



    columns_quant_order = ['PairInstrumentKey', 'ActiveTotalTradeAmount', 'PassiveTotalTradeAmount', 'ActiveTotalOrderAmount', 'PassiveTotalOrderAmount', 'ActiveTotalTradeOrderCount', 'PassiveTotalTradeOrderCount', 'ActiveTotalOrderCount', 'PassiveTotalOrderCount', 'ActiveTradeRate', 'PassiveTradeRate', 'ActiveLongRealSpread', 'ActiveShortRealSpread', 'TotalActiveSlippage', 'TotalPassiveSlippage', 'TotalActiveLongTradeAmount', 'TotalActiveLongTradePrice', 'TotalActiveShortTradeAmount', 'TotalActiveShortTradePrice', 'TotalPassiveLongTradeAmount', 'TotalPassiveLongTradePrice', 'TotalPassiveShortTradeAmount', 'TotalPassiveShortTradePrice']
    columns_pair_order = ['OpenLongTargetSpread', 'OpenShortTargetSpread', 'CloseLongTargetSpread', 'CloseShortTargetSpread', 'OpenLongRealSpread', 'OpenShortRealSpread', 'CloseLongRealSpread', 'CloseShortRealSpread', 'AvgActiveNewOrderSystemDealy', 'AvgActiveCancelOrderSystemDealy', 'AvgActiveNewOrderExchangeDealy', 'AvgPassiveNewOrderSystemDealy', 'AvgPassiveCancelOrderSystemDealy', 'AvgPassiveNewOrderExchangeDealy', 'AvgActiveDepthDelay', 'AvgPassiveDepthDelay', 'AvgMinActivePassiveDepthDelay', 'AvgActiveGenerateDelay', 'PairTotalVolume', 'PairActiveTotalPrice', 'PairPassiveTotalPrice']
    
    columns = []
    columns.extend(columns_quant_order)
    columns.extend(columns_pair_order)
    data = []

    '''
    ba_symbol_path = './ba_symbol.txt'
    f = open(ba_symbol_path, 'w')
    ba_symbol_list = []
    '''

    pair_instrument_list = []
    for pair_id in pair_order_statictis.pair_order_dict:
        pair_order = pair_order_statictis.pair_order_dict[pair_id]
        pair_instrument_key = f'{pair_order.active_instrument_key}|{pair_order.passive_instrument_key}'
        if pair_instrument_key not in pair_instrument_list:
            pair_instrument_list.append(pair_instrument_key)

        '''
        if 'BINANCE' in pair_order.active_instrument_key:
            ba_symbol = pair_order.active_instrument_key.split('.')[2].replace('-', '')
            if ba_symbol not in ba_symbol_list:
                ba_symbol_list.append(ba_symbol)

        if 'BINANCE' in pair_order.passive_instrument_key:
            ba_symbol = pair_order.passive_instrument_key.split('.')[2].replace('-', '')
            if ba_symbol not in ba_symbol_list:
                ba_symbol_list.append(ba_symbol)
        '''

    '''
    for ba_symbol in ba_symbol_list:
        f.write(f'{ba_symbol}\n')
    f.close()
    '''

    for pair_instrument_key in pair_instrument_list:
        row = []
        row.append(pair_instrument_key)
        row.append(pair_order_statictis.total_active_trade_amount.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_passive_trade_amount.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_active_order_amount.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_passive_order_amount.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_active_trade_order_count.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_passive_trade_order_count.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_active_order_count.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_passive_order_count.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_active_trade_rate.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_passive_trade_rate.get(pair_instrument_key, 0))

        active_long_price = pair_order_statictis.active_long_real_spread_price.get(pair_instrument_key, 0)
        passive_long_price = pair_order_statictis.passive_long_real_spread_price.get(pair_instrument_key, 0)
        
        long_real_spread = 0
        if long_real_spread != 0:
            long_real_spread = passive_long_price / active_long_price - 1

        active_short_price = pair_order_statictis.active_short_real_spread_price.get(pair_instrument_key, 0)
        passive_short_price = pair_order_statictis.passive_short_real_spread_price.get(pair_instrument_key, 0)

        short_real_spread = 0
        if active_short_price != 0:
            short_real_spread = passive_short_price / active_short_price - 1
        row.append(pair_order_statictis.active_long_real_spread.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.active_short_real_spread.get(pair_instrument_key, 0))
        #row.append(long_real_spread)
        #row.append(short_real_spread)

        row.append(pair_order_statictis.total_active_slippage.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_passive_slippage.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_active_long_trade_amount.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_active_long_trade_price.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_active_short_trade_amount.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_active_short_trade_price.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_passive_long_trade_amount.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_passive_long_trade_price.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_passive_short_trade_amount.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_passive_short_trade_price.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_open_long_target_spread.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_open_short_target_spread.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_close_long_target_spread.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_close_short_target_spread.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_open_long_real_spread.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_open_short_real_spread.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_close_long_real_spread.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.total_close_short_real_spread.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.avg_active_new_order_system_delay.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.avg_active_cancel_order_system_delay.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.avg_active_new_order_exchange_delay.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.avg_passive_new_order_system_delay.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.avg_passive_cancel_order_system_delay.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.avg_passive_new_order_exchange_delay.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.avg_active_depth_delay.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.avg_passive_depth_delay.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.avg_min_active_passive_depth_delay.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.avg_active_generate_delay.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.pair_total_volume.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.pair_active_total_price.get(pair_instrument_key, 0))
        row.append(pair_order_statictis.pair_passive_total_price.get(pair_instrument_key, 0))

        data.append(row)

    df = pd.DataFrame(data=data, columns=columns)
    df.set_index('PairInstrumentKey', inplace=True)
    df = df.sort_values(by='ActiveTotalTradeAmount', ascending=False)
    #df.to_csv('./Statictis_all.csv')
    df.to_csv(f'./{run_path}/Statictis_{date_str}.csv')

    end_time = datetime.now()

    total_time = end_time - start_time

    print(f'calc time: {total_time}')

    os.kill(os.getpid(), signal.SIGKILL)

