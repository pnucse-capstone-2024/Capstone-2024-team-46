import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from scipy.ndimage import gaussian_filter1d

# 4개의 데이터셋을 한번에 비교하는 코드
def multiple_plotting():
    # 파일 경로, 이름 설정해주세요.
    data1_df = pd.read_csv('data/label_0_data.csv', header=None)
    data2_df = pd.read_csv('data/label_1b_data.csv', header=None)
    data3_df = pd.read_csv('data/label_2_data.csv', header=None)
    data4_df = pd.read_csv('data/label_3_data.csv', header=None)

    data1_df.columns = ['x', 'y', 'z']
    data2_df.columns = ['x', 'y', 'z']
    data3_df.columns = ['x', 'y', 'z']
    data4_df.columns = ['x', 'y', 'z']


    vector_value1 = np.sqrt(data1_df['x']** 2 + data1_df['y']**2 + data1_df['z']**2)
    vector_value2 = np.sqrt(data2_df['x']** 2 + data2_df['y']**2 + data2_df['z']**2)
    vector_value3 = np.sqrt(data3_df['x']** 2 + data3_df['y']**2 + data3_df['z']**2)
    vector_value4 = np.sqrt(data4_df['x']** 2 + data4_df['y']**2 + data4_df['z']**2)

    plt.figure(figsize=(20,20))
    fig, axes = plt.subplots(nrows=2, ncols=2)

    axes[0][0].plot(vector_value1)
    axes[0][1].plot(vector_value2)
    axes[1][0].plot(vector_value3)
    axes[1][1].plot(vector_value4)
    plt.show()

def main():
    # # 파일 경로, 이름 설정해주세요.
    # data_df = pd.read_csv('./sensor_data_a.csv')
    # data_df.columns = ['x', 'y', 'z']

    # # L2 distance를 사용한 vector value
    # vector_value = np.sqrt(data_df['x'] ** 2 + data_df['y'] ** 2 + data_df['z'] ** 2)

    # # 표준편차인 sigma 값이 커질수록 high frequency가 없어져서 그래프가 덜 촘촘하게 됩니다.
    # sigma = .5 
    # filtered_vector_x = gaussian_filter1d(vector_value, sigma)

    # plt.figure(figsize=(10,10))
    # # gaussian filter를 거치지 않은 데이터를 plot하려면 그냥 vector_value를 넣어주세요.
    # plt.plot(filtered_vector_x)
    # plt.xlabel('time')
    # plt.ylabel('acceleration')
    # plt.show()

    multiple_plotting()


if __name__ == "__main__":
	main()



