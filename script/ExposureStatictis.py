#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
算法单敞口统计 / Exposure reconstruction from pairOrder + quantOrder CSV.

核心口径：
  1. 只统计“已成交”的量（totalVolumeOnOrder 的增量），不把挂单量当成持仓。
  2. 每条腿先按合约面值 value 换成 base asset，再按成交均价换成 USDT。
  3. 同一 pairId 内，主动腿和被动腿方向相反，按方向带符号累计；
     |累计净额| 的最大值就是这笔配对单在生命周期内的最大瞬时敞口。
  4. 同一 algoPairId 内跨 pairId 汇总，得到算法单级别的最大瞬时敞口。

注意：
  - CSV 里没有账户持仓快照，所以这里重建的是“订单成交口径”的敞口，
    不是交易所账户真实持仓。最终确认仍应拿 position / 风控日志对账。
  - value 优先从 SecurityManager 取；本机没有 /inc/version.inc 时用
    DEFAULT_CONTRACT_VALUES 兜底（当前只覆盖 DOGE 的 Binance=1、Gate=10）。
"""

import os
from typing import Dict

import numpy as np
import pandas as pd


DEFAULT_CONTRACT_VALUES = {
    "BINANCE.USDT_SWAP.DOGE-USDT": 1.0,
    "GATEIO.USDT_SWAP.DOGE-USDT": 10.0,
}


def _to_bool(series: pd.Series) -> pd.Series:
    return series.astype(str).str.lower().isin(["true", "1", "1.0"])


def _to_int64(series: pd.Series) -> pd.Series:
    return pd.to_numeric(series, errors="coerce").fillna(0).astype("int64")


def _fmt_us(ts) -> str:
    if ts is None or pd.isna(ts) or int(ts) <= 0:
        return ""
    return pd.to_datetime(int(ts), unit="us").strftime("%Y-%m-%d %H:%M:%S.%f")


class ExposureStatictis:
    def __init__(self, value_getter=None):
        self._value_getter = value_getter or self._default_value_getter
        self._value_cache: Dict[str, float] = {}
        self.pair_order_df = pd.DataFrame()
        self.quant_order_df = pd.DataFrame()
        self.events = pd.DataFrame()
        self.pair_metrics = pd.DataFrame()
        self.algo_metrics = pd.DataFrame()
        self.summary = {}

    # ------------------------------------------------------------------
    # value
    # ------------------------------------------------------------------
    @staticmethod
    def _default_value_getter(instrument_key: str) -> float:
        # 放在函数内 import，避免本机没有 Object.py 依赖时整个脚本起不来。
        try:
            from Object import smc  # type: ignore

            parts = instrument_key.split(".")
            if len(parts) >= 3:
                ok, info = smc.get_instrument_info(parts[0], parts[1], parts[2])
                if ok and info and float(info.get("value", 0) or 0) > 0:
                    return float(info["value"])
        except Exception:
            pass
        return float(DEFAULT_CONTRACT_VALUES.get(instrument_key, 1.0))

    def get_value(self, instrument_key: str) -> float:
        if instrument_key not in self._value_cache:
            self._value_cache[instrument_key] = float(self._value_getter(instrument_key))
        return self._value_cache[instrument_key]

    # ------------------------------------------------------------------
    # load / calc
    # ------------------------------------------------------------------
    def load_file(self, pair_order_path: str, quant_order_path: str):
        self.pair_order_df = pd.read_csv(
            pair_order_path, converters={"pairId": str, "algoPairId": str}
        )
        self.quant_order_df = pd.read_csv(
            quant_order_path, converters={"pairId": str, "algoPairId": str}
        )
        return self

    def calc(self):
        if self.quant_order_df.empty:
            self.pair_metrics = pd.DataFrame()
            self.algo_metrics = pd.DataFrame()
            self.summary = {"totalPairs": 0}
            return self

        events, order_final = self._build_events()
        self.events = events
        if events.empty:
            self.pair_metrics = pd.DataFrame()
            self.algo_metrics = pd.DataFrame()
            self.summary = {"totalPairs": 0}
            return self

        pair_info = self._build_pair_info()
        pair_metrics = self._calc_pair_metrics(events, order_final, pair_info)
        self.pair_metrics = pair_metrics
        self.algo_metrics = self._calc_algo_metrics(events, pair_metrics)
        self.summary = self._build_summary(events, pair_metrics, self.algo_metrics)
        return self

    def _build_events(self):
        df = self.quant_order_df.copy()
        for col in ("pairId", "algoPairId", "strategyOrderId"):
            if col in df.columns:
                df[col] = df[col].astype(str)

        df["updateTime"] = _to_int64(df["updateTime"])
        df["totalVolumeOnOrder"] = (
            pd.to_numeric(df["totalVolumeOnOrder"], errors="coerce")
            .fillna(0.0)
            .clip(lower=0.0)
        )
        df["totalPriceOnOrder"] = pd.to_numeric(
            df["totalPriceOnOrder"], errors="coerce"
        ).fillna(0.0)
        df["price"] = pd.to_numeric(df["price"], errors="coerce").fillna(0.0)
        df["isActiveOrder"] = _to_bool(df["isActiveOrder"])
        df["directionSign"] = np.where(
            df["direction"].astype(str).str.contains("LONG"), 1.0, -1.0
        )
        df["value"] = df["instrumentKey"].map(self.get_value).astype(float)

        df = df.sort_values(["strategyOrderId", "updateTime"], kind="mergesort")
        df["prevFilled"] = (
            df.groupby("strategyOrderId", sort=False)["totalVolumeOnOrder"]
            .shift()
            .fillna(0.0)
        )
        df["fillDelta"] = (df["totalVolumeOnOrder"] - df["prevFilled"]).clip(lower=0.0)

        events = df[df["fillDelta"] > 1e-12].copy()
        events["fillPrice"] = np.where(
            events["totalPriceOnOrder"] > 0,
            events["totalPriceOnOrder"],
            events["price"],
        )
        events["fillBase"] = events["fillDelta"] * events["value"]
        events["fillNotional"] = events["fillBase"] * events["fillPrice"]
        # 敞口必须用 base asset 的净持仓衡量，再乘一个共同价格；
        # 不能直接用主动/被动成交额相减，否则会把两条腿的成交价差当成持仓敞口。
        events["signedBase"] = events["fillBase"] * events["directionSign"]
        events = events.sort_values(
            ["pairId", "updateTime", "isActiveOrder"],
            ascending=[True, True, False],
            kind="mergesort",
        )

        # 每个 strategyOrderId 的最终状态 / 累计成交
        order_final = df.groupby("strategyOrderId", sort=False).tail(1).copy()
        order_final["filledBase"] = (
            order_final["totalVolumeOnOrder"] * order_final["value"]
        )
        order_final["filledNotional"] = (
            order_final["filledBase"] * order_final["totalPriceOnOrder"]
        )

        first_fill = events.groupby("strategyOrderId", sort=False)["updateTime"].min()
        last_fill = events.groupby("strategyOrderId", sort=False)["updateTime"].max()
        order_final["firstFillTime"] = order_final["strategyOrderId"].map(first_fill)
        order_final["lastFillTime"] = order_final["strategyOrderId"].map(last_fill)
        return events, order_final

    def _build_pair_info(self):
        if self.pair_order_df.empty:
            return pd.DataFrame(
                columns=[
                    "pairId",
                    "algoPairId",
                    "activeInstrumentKey",
                    "passiveInstrumentKey",
                    "tradingTypeOrder",
                    "tradingTypeOffset",
                    "targetVolume",
                ]
            )

        p = self.pair_order_df.copy()
        if "updateTime" in p.columns:
            p["updateTime"] = _to_int64(p["updateTime"])
            p = p.sort_values("updateTime")
        p = p.drop_duplicates(subset=["pairId"], keep="last")
        keep = [
            "pairId",
            "algoPairId",
            "activeInstrumentKey",
            "passiveInstrumentKey",
            "tradingTypeOrder",
            "tradingTypeOffset",
            "targetVolume",
            "activeTargetPrice",
            "passiveTargetPrice",
            "activeAccountId",
            "passiveAccountId",
        ]
        keep = [c for c in keep if c in p.columns]
        return p[keep].copy()

    def _calc_pair_metrics(self, events, order_final, pair_info):
        active_events = events[events["isActiveOrder"]]
        passive_events = events[~events["isActiveOrder"]]

        active_agg = active_events.groupby("pairId", sort=False).agg(
            ActiveFilledBase=("fillBase", "sum"),
            ActiveFilledNotional=("fillNotional", "sum"),
            FirstActiveFillTime=("updateTime", "min"),
            LastActiveFillTime=("updateTime", "max"),
        )
        passive_agg = passive_events.groupby("pairId", sort=False).agg(
            PassiveFilledBase=("fillBase", "sum"),
            PassiveFilledNotional=("fillNotional", "sum"),
            FirstPassiveFillTime=("updateTime", "min"),
            LastPassiveFillTime=("updateTime", "max"),
        )

        # 同一 pairId 内按成交事件重放，取累计净额绝对值的最大值。
        timeline_rows = []
        for pid, g in events.groupby("pairId", sort=False):
            cum_base = 0.0
            last_price = 0.0
            max_abs = 0.0
            max_signed = 0.0
            max_time = 0
            max_order_id = ""
            max_instrument = ""
            max_is_active = False
            exposure_usdt_seconds = 0.0
            prev_time = None
            for r in g.itertuples(index=False):
                t = int(r.updateTime)
                # 上一次成交后到本次成交前，净 base 持仓不变，按上一笔价格计敞口。
                if prev_time is not None:
                    exposure_usdt_seconds += (
                        abs(cum_base * last_price) * (t - prev_time) / 1_000_000.0
                    )
                cum_base += float(r.signedBase)
                if float(r.fillPrice) > 0:
                    last_price = float(r.fillPrice)
                exp_notional = cum_base * last_price
                if abs(exp_notional) > abs(max_abs):
                    max_abs = abs(exp_notional)
                    max_signed = exp_notional
                    max_time = t
                    max_order_id = str(r.strategyOrderId)
                    max_instrument = str(r.instrumentKey)
                    max_is_active = bool(r.isActiveOrder)
                prev_time = t
            timeline_rows.append(
                {
                    "pairId": pid,
                    "MaxExposureNotional": max_abs,
                    "MaxExposureSigned": max_signed,
                    "MaxExposureTime": max_time,
                    "MaxExposureOrderId": max_order_id,
                    "MaxExposureInstrumentKey": max_instrument,
                    "MaxExposureIsActive": max_is_active,
                    "ExposureUsdtSeconds": exposure_usdt_seconds,
                    "LastMarkPrice": last_price,
                }
            )
        timeline = pd.DataFrame(timeline_rows)

        active_orders = order_final[order_final["isActiveOrder"]]
        passive_orders = order_final[~order_final["isActiveOrder"]]
        active_order_agg = active_orders.groupby("pairId", sort=False).agg(
            ActiveOrderCount=("strategyOrderId", "size"),
            ActiveFilledOrderCount=("filledBase", lambda s: int((s > 1e-12).sum())),
        )
        passive_order_agg = passive_orders.groupby("pairId", sort=False).agg(
            PassiveOrderCount=("strategyOrderId", "size"),
            PassiveFilledOrderCount=("filledBase", lambda s: int((s > 1e-12).sum())),
            PassiveCanceledCount=(
                "orderStatus",
                lambda s: int(s.isin(["OS_CANCELED", "OS_CANCEL"]).sum()),
            ),
            PassiveRejectedCount=(
                "orderStatus",
                lambda s: int((s == "OS_REJECTED").sum()),
            ),
        )

        m = pair_info.merge(active_agg, on="pairId", how="left")
        m = m.merge(passive_agg, on="pairId", how="left")
        m = m.merge(timeline, on="pairId", how="left")
        m = m.merge(active_order_agg, on="pairId", how="left")
        m = m.merge(passive_order_agg, on="pairId", how="left")

        num_cols = [
            "ActiveFilledBase",
            "PassiveFilledBase",
            "ActiveFilledNotional",
            "PassiveFilledNotional",
            "MaxExposureNotional",
            "MaxExposureSigned",
            "ActiveOrderCount",
            "PassiveOrderCount",
            "ActiveFilledOrderCount",
            "PassiveFilledOrderCount",
            "PassiveCanceledCount",
            "PassiveRejectedCount",
            "ExposureUsdtSeconds",
            "LastMarkPrice",
        ]
        for c in num_cols:
            if c not in m.columns:
                m[c] = 0.0
            m[c] = pd.to_numeric(m[c], errors="coerce").fillna(0.0)

        for c in (
            "FirstActiveFillTime",
            "FirstPassiveFillTime",
            "LastActiveFillTime",
            "LastPassiveFillTime",
            "MaxExposureTime",
        ):
            if c not in m.columns:
                m[c] = 0
            m[c] = pd.to_numeric(m[c], errors="coerce").fillna(0).astype("int64")

        # 主动腿/被动腿的方向不一定都写成“主动=正、被动=负”：
        # 例如 CLOSE_SHORT 是主动 DT_SHORT + 被动 DT_LONG，直接
        # ActiveFilledBase - PassiveFilledBase 会把符号算反。
        # 这里必须用成交事件里已经带方向的 signedBase 重新汇总。
        signed_net_by_pair = events.groupby("pairId", sort=False)["signedBase"].sum()
        m["FinalNetBase"] = m["pairId"].map(signed_net_by_pair).fillna(0.0)
        m["FinalNetNotional"] = m["FinalNetBase"] * m["LastMarkPrice"]
        m["FillNotionalDiff"] = (
            m["ActiveFilledNotional"] - m["PassiveFilledNotional"]
        )
        m["HedgeLagMs"] = np.where(
            (m["FirstActiveFillTime"] > 0) & (m["FirstPassiveFillTime"] > 0),
            (m["FirstPassiveFillTime"] - m["FirstActiveFillTime"]) / 1000.0,
            np.nan,
        )
        m["FullHedgeLagMs"] = np.where(
            (m["FirstActiveFillTime"] > 0) & (m["LastPassiveFillTime"] > 0),
            (m["LastPassiveFillTime"] - m["FirstActiveFillTime"]) / 1000.0,
            np.nan,
        )
        m["PassiveRetryCount"] = (
            m["PassiveCanceledCount"] + m["PassiveRejectedCount"]
        )
        active_key = (
            m["activeInstrumentKey"].astype(str)
            if "activeInstrumentKey" in m.columns
            else pd.Series("", index=m.index)
        )
        passive_key = (
            m["passiveInstrumentKey"].astype(str)
            if "passiveInstrumentKey" in m.columns
            else pd.Series("", index=m.index)
        )
        m["PairInstrumentKey"] = active_key + "|" + passive_key

        def risk_flag(row):
            flags = []
            if abs(float(row["FinalNetNotional"])) >= 1.0:
                flags.append("RESIDUAL")
            if float(row["PassiveRejectedCount"]) > 0:
                flags.append("PASSIVE_REJECT")
            high_exposure = float(row["MaxExposureNotional"]) >= 20.0
            if (
                high_exposure
                and pd.notna(row["FullHedgeLagMs"])
                and float(row["FullHedgeLagMs"]) >= 1000.0
            ):
                flags.append("SLOW_HEDGE")
            elif (
                high_exposure
                and pd.notna(row["FullHedgeLagMs"])
                and float(row["FullHedgeLagMs"]) >= 100.0
            ):
                flags.append("HEDGE_LAG")
            return "|".join(flags)

        m["RiskFlag"] = m.apply(risk_flag, axis=1)
        m["MaxExposureTimeStr"] = m["MaxExposureTime"].map(_fmt_us)
        m["FirstActiveFillTimeStr"] = m["FirstActiveFillTime"].map(_fmt_us)
        m["FirstPassiveFillTimeStr"] = m["FirstPassiveFillTime"].map(_fmt_us)
        m["LastActiveFillTimeStr"] = m["LastActiveFillTime"].map(_fmt_us)
        m["LastPassiveFillTimeStr"] = m["LastPassiveFillTime"].map(_fmt_us)

        rename = {
            "pairId": "PairId",
            "algoPairId": "AlgoPairId",
            "activeInstrumentKey": "ActiveInstrumentKey",
            "passiveInstrumentKey": "PassiveInstrumentKey",
            "tradingTypeOrder": "TradingTypeOrder",
            "tradingTypeOffset": "TradingTypeOffset",
            "targetVolume": "TargetVolume",
            "activeTargetPrice": "ActiveTargetPrice",
            "passiveTargetPrice": "PassiveTargetPrice",
            "activeAccountId": "ActiveAccountId",
            "passiveAccountId": "PassiveAccountId",
        }
        m = m.rename(columns=rename)
        if "TargetVolume" in m.columns and "ActiveTargetPrice" in m.columns:
            m["TargetNotional"] = (
                pd.to_numeric(m["TargetVolume"], errors="coerce").fillna(0.0)
                * pd.to_numeric(m["ActiveTargetPrice"], errors="coerce").fillna(0.0)
            )
        else:
            m["TargetNotional"] = 0.0

        out_cols = [
            "PairId",
            "AlgoPairId",
            "PairInstrumentKey",
            "TradingTypeOrder",
            "TradingTypeOffset",
            "TargetVolume",
            "TargetNotional",
            "ActiveTargetPrice",
            "PassiveTargetPrice",
            "ActiveAccountId",
            "PassiveAccountId",
            "ActiveFilledBase",
            "PassiveFilledBase",
            "FinalNetBase",
            "ActiveFilledNotional",
            "PassiveFilledNotional",
            "FillNotionalDiff",
            "FinalNetNotional",
            "MaxExposureNotional",
            "MaxExposureSigned",
            "MaxExposureTimeStr",
            "MaxExposureOrderId",
            "MaxExposureInstrumentKey",
            "MaxExposureIsActive",
            "FirstActiveFillTimeStr",
            "FirstPassiveFillTimeStr",
            "LastActiveFillTimeStr",
            "LastPassiveFillTimeStr",
            "HedgeLagMs",
            "FullHedgeLagMs",
            "ExposureUsdtSeconds",
            "ActiveOrderCount",
            "PassiveOrderCount",
            "ActiveFilledOrderCount",
            "PassiveFilledOrderCount",
            "PassiveCanceledCount",
            "PassiveRejectedCount",
            "PassiveRetryCount",
            "RiskFlag",
        ]
        out_cols = [c for c in out_cols if c in m.columns]
        m = m[out_cols].sort_values(
            ["ExposureUsdtSeconds", "MaxExposureNotional", "FullHedgeLagMs"],
            ascending=[False, False, False],
            kind="mergesort",
        )
        return m.reset_index(drop=True)

    def _calc_algo_metrics(self, events, pair_metrics):
        if events.empty:
            return pd.DataFrame()

        # algo 级别也要按事件重放，不能简单把每笔 pair 的 max 相加。
        rows = []
        for aid, g in events.groupby("algoPairId", sort=False):
            g = g.sort_values(["updateTime", "isActiveOrder"], ascending=[True, False])
            cum_base = 0.0
            last_price = 0.0
            max_abs = 0.0
            max_signed = 0.0
            max_time = 0
            for r in g.itertuples(index=False):
                cum_base += float(r.signedBase)
                if float(r.fillPrice) > 0:
                    last_price = float(r.fillPrice)
                exp_notional = cum_base * last_price
                if abs(exp_notional) > abs(max_abs):
                    max_abs = abs(exp_notional)
                    max_signed = exp_notional
                    max_time = int(r.updateTime)
            rows.append(
                {
                    "AlgoPairId": aid,
                    "MaxExposureNotional": max_abs,
                    "MaxExposureSigned": max_signed,
                    "MaxExposureTimeStr": _fmt_us(max_time),
                }
            )
        algo = pd.DataFrame(rows)

        if pair_metrics.empty:
            return algo

        agg = pair_metrics.groupby("AlgoPairId", sort=False).agg(
            PairCount=("PairId", "size"),
            ActiveFilledNotional=("ActiveFilledNotional", "sum"),
            PassiveFilledNotional=("PassiveFilledNotional", "sum"),
            FinalNetNotional=("FinalNetNotional", "sum"),
            MaxPairExposureNotional=("MaxExposureNotional", "max"),
            MaxHedgeLagMs=("HedgeLagMs", "max"),
            MaxFullHedgeLagMs=("FullHedgeLagMs", "max"),
            ExposureUsdtSeconds=("ExposureUsdtSeconds", "sum"),
            PassiveRejectedCount=("PassiveRejectedCount", "sum"),
            PassiveCanceledCount=("PassiveCanceledCount", "sum"),
            SlowHedgePairs=("FullHedgeLagMs", lambda s: int((s >= 1000.0).sum())),
        )
        out = agg.reset_index().merge(algo, on="AlgoPairId", how="left")
        return out.sort_values(
            "MaxExposureNotional", ascending=False, kind="mergesort"
        ).reset_index(drop=True)

    def _build_summary(self, events, pair_metrics, algo_metrics):
        if pair_metrics.empty:
            return {"totalPairs": 0}

        # 全局口径：所有 pair 的成交事件按时间重放。
        cum_base = 0.0
        last_price = 0.0
        max_abs = 0.0
        max_signed = 0.0
        max_time = 0
        for r in events.sort_values("updateTime").itertuples(index=False):
            cum_base += float(r.signedBase)
            if float(r.fillPrice) > 0:
                last_price = float(r.fillPrice)
            exp_notional = cum_base * last_price
            if abs(exp_notional) > abs(max_abs):
                max_abs = abs(exp_notional)
                max_signed = exp_notional
                max_time = int(r.updateTime)

        slow = pair_metrics[pair_metrics["FullHedgeLagMs"] >= 1000.0]
        very_slow = pair_metrics[pair_metrics["FullHedgeLagMs"] >= 3000.0]
        over_95 = pair_metrics[pair_metrics["MaxExposureNotional"] >= 95.0]
        over_95_slow = pair_metrics[
            (pair_metrics["MaxExposureNotional"] >= 95.0)
            & (pair_metrics["FullHedgeLagMs"] >= 1000.0)
        ]
        residual = pair_metrics[pair_metrics["FinalNetNotional"].abs() >= 1.0]
        filled_pairs = pair_metrics[
            (pair_metrics["ActiveFilledNotional"] > 1e-12)
            | (pair_metrics["PassiveFilledNotional"] > 1e-12)
        ]

        return {
            "totalPairs": int(len(pair_metrics)),
            "filledPairs": int(len(filled_pairs)),
            "totalAlgos": int(pair_metrics["AlgoPairId"].nunique())
            if "AlgoPairId" in pair_metrics
            else 0,
            "totalActiveNotional": float(pair_metrics["ActiveFilledNotional"].sum()),
            "totalPassiveNotional": float(pair_metrics["PassiveFilledNotional"].sum()),
            "finalNetNotional": float(pair_metrics["FinalNetNotional"].sum()),
            "finalNetBase": float(pair_metrics["FinalNetBase"].sum()),
            "maxPairExposure": float(pair_metrics["MaxExposureNotional"].max()),
            "maxPairExposurePairId": str(
                pair_metrics.loc[
                    pair_metrics["MaxExposureNotional"].idxmax(), "PairId"
                ]
            ),
            "maxAlgoExposure": float(algo_metrics["MaxExposureNotional"].max())
            if not algo_metrics.empty
            else 0.0,
            "maxGlobalExposure": float(max_abs),
            "maxGlobalExposureSigned": float(max_signed),
            "maxGlobalExposureTime": _fmt_us(max_time),
            "totalExposureUsdtSeconds": float(
                pair_metrics["ExposureUsdtSeconds"].sum()
            ),
            "slowHedgePairs": int(len(slow)),
            "verySlowHedgePairs": int(len(very_slow)),
            "over95Pairs": int(len(over_95)),
            "over95SlowPairs": int(len(over_95_slow)),
            "maxHedgeLagMs": float(pair_metrics["HedgeLagMs"].max())
            if pair_metrics["HedgeLagMs"].notna().any()
            else 0.0,
            "maxFullHedgeLagMs": float(pair_metrics["FullHedgeLagMs"].max())
            if pair_metrics["FullHedgeLagMs"].notna().any()
            else 0.0,
            "passiveRejectedOrders": int(pair_metrics["PassiveRejectedCount"].sum()),
            "passiveCanceledOrders": int(pair_metrics["PassiveCanceledCount"].sum()),
            "residualPairs": int(len(residual)),
            "maxFinalNetNotional": float(pair_metrics["FinalNetNotional"].abs().max()),
            "firstEventTime": _fmt_us(int(events["updateTime"].min())),
            "lastEventTime": _fmt_us(int(events["updateTime"].max())),
        }

    # ------------------------------------------------------------------
    # output
    # ------------------------------------------------------------------
    def write_report(self, output_path: str):
        if self.pair_metrics.empty:
            return self.summary

        os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
        self.pair_metrics.to_csv(output_path, index=False, encoding="utf-8-sig")
        algo_path = output_path[:-4] + "_algo.csv" if output_path.endswith(".csv") else output_path + "_algo.csv"
        self.algo_metrics.to_csv(algo_path, index=False, encoding="utf-8-sig")
        return self.summary

    def summary_text(self) -> str:
        s = self.summary
        if not s or s.get("totalPairs", 0) == 0:
            return "[exposure] no filled pair orders"

        top = self.pair_metrics.head(10)
        lines = [
            "[exposure] pairs={totalPairs} filledPairs={filledPairs} algos={totalAlgos} "
            "active={totalActiveNotional:.2f}u passive={totalPassiveNotional:.2f}u "
            "finalNet={finalNetNotional:.4f}u".format(**s),
            "[exposure] maxPair={maxPairExposure:.4f}u pair={maxPairExposurePairId} "
            "maxAlgo={maxAlgoExposure:.4f}u maxGlobal={maxGlobalExposure:.4f}u "
            "at={maxGlobalExposureTime}".format(**s),
            "[exposure] slowHedge>=1s={slowHedgePairs} >=3s={verySlowHedgePairs} "
            "over95u={over95Pairs} over95u&slow={over95SlowPairs} "
            "passiveReject={passiveRejectedOrders} passiveCancel={passiveCanceledOrders} "
            "residual>=1u={residualPairs}".format(**s),
            "[exposure] totalExposure={totalExposureUsdtSeconds:.2f} u*s "
            "maxHedgeLag={maxHedgeLagMs:.1f}ms "
            "maxFullHedgeLag={maxFullHedgeLagMs:.1f}ms".format(**s),
            "[exposure] top pairs by exposure u*s:",
        ]
        cols = [
            "PairId",
            "AlgoPairId",
            "MaxExposureNotional",
            "MaxExposureTimeStr",
            "MaxExposureOrderId",
            "MaxExposureInstrumentKey",
            "ExposureUsdtSeconds",
            "HedgeLagMs",
            "FullHedgeLagMs",
            "PassiveRejectedCount",
            "FinalNetNotional",
            "RiskFlag",
        ]
        cols = [c for c in cols if c in top.columns]
        lines.append(top[cols].to_string(index=False))
        return "\n".join(lines)
