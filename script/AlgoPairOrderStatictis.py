from Object import AlgoPairOrder
import pandas as pd


class AlgoPairOrderStatictis:
    def __init__(self):
        self.algo_pair_order_dict = {}

    def load_file(self, file_path):
        df = pd.read_csv(file_path)
        norepeat_df = df.drop_duplicates(subset=['algoOrderId'], keep='last')

        for index, row in norepeat_df.iterrows():
            algo_pair_order = AlgoPairOrder()
            algo_pair_order.algo_type = row['algoType']
            algo_pair_order.algo_strategy_name = row['algoStrategyName']
            algo_pair_order.algo_order_id = row['algoOrderId']
            algo_pair_order.pair_instrument_key = row['pairInstrumentKey']
            algo_pair_order.base_asset = row['baseAsset']
            algo_pair_order.algo_order_status = row['algoOrderStatus']
            algo_pair_order.active_instrument_key = row['activeInstrumentKey']
            algo_pair_order.active_price_taker_pct = row['activePriceTakerPct']
            algo_pair_order.active_price_maker_pct = row['activePriceMakerPct']
            algo_pair_order.active_account_id = row['activeAccountId']
            algo_pair_order.active_drive_type = row['activeDriveType']
            algo_pair_order.active_depth_maker_check = row['activeDepthMakerCheck']
            algo_pair_order.active_depth_taker_check = row['activeDepthTakerCheck']
            algo_pair_order.active_depth_maker_check_type = row['activeDepthMakerCheckType']
            algo_pair_order.active_depth_taker_check_type = row['activeDepthTakerCheckType']
            algo_pair_order.active_order_type = row['activeOrderType']
            algo_pair_order.passive_instrument_key = row['passiveInstrumentKey']
            algo_pair_order.passive_price_taker_pct = row['passivePriceTakerPct']
            algo_pair_order.passive_price_maker_pct = row['passivePriceMakerPct']
            algo_pair_order.passive_account_id = row['passiveAccountId']
            algo_pair_order.passive_drive_type = row['passiveDriveType']
            algo_pair_order.passive_depth_maker_check = row['passiveDepthMakerCheck']
            algo_pair_order.passive_depth_taker_check = row['passiveDepthTakerCheck']
            algo_pair_order.passive_depth_maker_check_type = row['passiveDepthMakerCheckType']
            algo_pair_order.passive_depth_taker_check_type = row['passiveDepthTakerCheckType']
            algo_pair_order.passive_order_type = row['passiveOrderType']
            algo_pair_order.passive_volume_pct = row['passiveVolumePct']
            algo_pair_order.active_maker_cancel_order_time = row['activeMakerCancelOrderTime']
            algo_pair_order.active_taker_cancel_order_time = row['activeTakerCancelOrderTime']
            algo_pair_order.passive_maker_cancel_order_time = row['passiveMakerCancelOrderTime']
            algo_pair_order.passive_taker_cancel_order_time = row['passiveTakerCancelOrderTime']
            algo_pair_order.active_passive_cancel_order_pct = row['activePassiveCancelOrderPct']
            algo_pair_order.active_maker_cancel_order_pct = row['activeMakerCancelOrderPct']
            algo_pair_order.active_taker_cancel_order_pct = row['activeTakerCancelOrderPct']
            algo_pair_order.passive_maker_cancel_order_pct = row['passiveMakerCancelOrderPct']
            algo_pair_order.passive_taker_cancel_order_pct = row['passiveTakerCancelOrderPct']
            algo_pair_order.active_maker_fee_rate = row['activeMakerFeeRate']
            algo_pair_order.active_taker_fee_rate = row['activeTakerFeeRate']
            algo_pair_order.passive_maker_fee_rate = row['passiveMakerFeeRate']
            algo_pair_order.passive_taker_fee_rate = row['passiveTakerFeeRate']
            algo_pair_order.active_taker_slippage = row['activeTakerSlippage']
            algo_pair_order.active_maker_slippage = row['activeMakerSlippage']
            algo_pair_order.passive_taker_slippage = row['passiveTakerSlippage']
            algo_pair_order.passive_maker_slippage = row['passiveMakerSlippage']
            algo_pair_order.pair_active_total_price = row['pairActiveTotalPrice']
            algo_pair_order.pair_total_volume = row['pairTotalVolume']
            algo_pair_order.pair_passive_total_price = row['pairPassiveTotalPrice']
            algo_pair_order.maker_taker_fs = row['makerTakerFs']
            algo_pair_order.taker_taker_fs = row['takerTakerFs']
            algo_pair_order.max_mt_order_size = row['maxMTOrderSize']
            algo_pair_order.max_tt_order_size = row['maxTTOrderSize']
            algo_pair_order.target_spread_type = row['targetSpreadType']
            algo_pair_order.active_volume_calculate_type = row['activeVolumeCalcualteType']
            algo_pair_order.tt_target_volume = row['ttTargetVolume']
            algo_pair_order.mt_target_volume = row['mtTargetVolume']

            self.algo_pair_order_dict[algo_pair_order.algo_order_id] = algo_pair_order
