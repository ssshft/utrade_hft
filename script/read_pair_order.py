import pandas as pd
import sys

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('---error para----')
        sys.exit(0)

    pair_order_path = sys.argv[1]
    df = pd.read_csv(pair_order_path)
    #df = df[df['activeInstrumentKey'] == 'BYBIT.InstType_USDT_SWAP.STORJ-USDT']
    df = df[df['passiveInstrumentKey'] == 'BINANCE.InstType_USDT_SWAP.LQTY-USDT']
    df_0 = df[df['status'] == 0]
    df_1 = df[df['status'] == 1]
    res = pd.merge(df_0, df_1, on='pairId')
    res.to_csv('merge.csv')
    sys.exit(0)
    pair_id_dict = {}
    for index,row in df.iterrows():
        pair_id = row['pairId']
        if pair_id not in pair_id_dict:
            pair_id_dict[pair_id] = 1
        else:
            pair_id_dict[pair_id] += 1
    df.to_csv('./LQTY_passive_pair_order.csv')
    for key,value in pair_id_dict.items():
        if value < 2:
            print(key)
