"""
File    : Server with data logging.py
Author  : Sooyeon Kim
Date    : September 17, 2023
Update  : April 17, 2024
Description : This script is a server program for receiving FBG sensor data over TCP/IP from a client. It also logs the data to a CSV file.

Protocol:
- Each data packet is 11 bytes:
    - 1st byte: Channel number (1-4)
    - 2nd byte: Fiber number
    - 3rd byte: Sensor number (1 or 2)
    - Remaining 8 bytes: FBG data (double precision, 64-bit floating point)
"""


import socket
import struct
import time
from datetime import datetime
import threading
import csv


csv_file_path = "C:/Users/user/Desktop/Data.csv"

### Setting for TCP/IP communication
PORT = 4578
FBG_PACKET_SIZE = 11

server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server_socket.bind(("0.0.0.0", PORT))
server_socket.listen(1)  # Listen for incoming connections
print("Server waiting for client connection...")

client_socket, addr = server_socket.accept()  # Accept a connection from a client
print(f"Connection from {addr}")

channel_num = [1, 2]  # (I4에서 오픈한 채널) 0~3
### Data
received_FBGs_data = b''  # 빈 바이트 문자열로 초기화
id_val = [[channel_num[0], 0, 1], [channel_num[1], 0, 1]]  # [Channel, Sensor1, Sensor2]

force1, force2, force3, force4 = 0, 0, 0, 0

# Event to signal when to stop the program
exit_event = threading.Event()

### Thread for TCP communication
def receive_FBGs_data():
    global received_FBGs_data, force1, force2, force3, force4

    while not exit_event.is_set():
        try:
            chunk = client_socket.recv(FBG_PACKET_SIZE)
            if not chunk:
                # Client connection closed
                raise ConnectionError("Connection closed unexpectedly")
        except Exception as e:
            exit_event.is_set()
            print(f"Error in receive_data: {e}")
            break

        received_data = chunk

        if len(received_data) >= FBG_PACKET_SIZE:
            id_info = struct.unpack('BBB', received_data[:3])
            FBGs = struct.unpack('d', received_data[3:])[0]

            # Update force values based on sensor and channel
            if id_info[0] == id_val[0][0]:  # 1st channel
                if id_info[2] == id_val[0][1]:  # 1st sensor
                    force1 = FBGs
                elif id_info[2] == id_val[0][2]:  # 2nd sensor
                    force2 = FBGs
            elif id_info[0] == id_val[1][0]:  # 2nd channel
                if id_info[2] == id_val[1][1]:  # 1st sensor
                    force3 = FBGs
                elif id_info[2] == id_val[1][2]:  # 2nd sensor
                    force4 = FBGs

            ## Print received data
            print(f"{id_info[1]}:: Sensor ID: {id_info[2]}, Channel: {id_info[0]}, FBGs: {FBGs} nm")

    # Close client and server sockets when thread ends
    client_socket.close()
    server_socket.close()



### Thread for data logging
log_lock = threading.Lock()
log_freq = 10

def log_data(log_lock, csv_file_path):
    global force1, force2, force3, force4

    with open(csv_file_path, mode='w', newline='') as csv_file:
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(['Time', 'Channel 1', '', 'Channel 2', ''])
        csv_writer.writerow(['', 'Sensor 1', 'Sensor 2', 'Sensor 1', 'Sensor 2'])


    while not exit_event.is_set():

        start_time = time.time()
        time.sleep(max(0, 1/log_freq - (time.time() - start_time)))

        data_to_write = []
        data_to_write.append([datetime.now(), force1, force2, force3, force4])

        with log_lock:
            with open(csv_file_path, mode='a', newline='') as csv_file:
                csv_writer = csv.writer(csv_file)
                csv_writer.writerows(data_to_write)


    csv_file.close()
    print("Logging stopped.")


def main():
    threads = []
    ## Threads
    threads.append(threading.Thread(target=receive_FBGs_data))
    threads.append(threading.Thread(target=log_data, args=(log_lock, csv_file_path)))

    for thread in threads:
        thread.start()

    try:
        for thread in threads:
            thread.join()
    
    except KeyboardInterrupt:
        exit_event.set()
        print("Exiting...")

        # Wait for threads to finish
        for thread in threads:
            thread.join()

if __name__ == "__main__":
    main()
