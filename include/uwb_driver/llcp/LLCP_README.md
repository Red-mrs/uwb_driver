# MRS LLCP

LLCP (low level communication protocol) is a library which simplifies and standardizes communication with UART based low level devices.
* See [mrs_llcp_ros](https://github.com/ctu-mrs/mrs_llcp_ros) - a ROS node which handles the serial port for you.
* See [llcp_example](https://github.com/ctu-mrs/llcp_example) to see an example ROS node (which communicates with mrs_llcp_ros) and an Arduino code where mrs_llcp is used.

In general, this is a structure you should be adhering to:

```mermaid
flowchart LR
A[Your ROS node] <-->|ROS messages| B[mrs_llcp_ros]
B <-->|UART| C[Low level device]
```

LLCP itself is written in in plain C, to ensure compability with as many devices as possible.
LLCP allows you to communicate with pre-defined messages, represented as C structs.
Here is an example definition of messages used in LLCP:

```c
#define DATA_MSG_ID 52
#define HEARTBEAT_MSG_ID 51

struct __attribute__((__packed__)) data_msg
{
  uint8_t  id;
  uint8_t  data1_uint8;
  uint32_t data2_uint32;
  float    data3_float;
};

struct __attribute__((__packed__)) heartbeat_msg
{
  uint8_t  id;
  bool     is_running;
  uint16_t messages_received;
};
```

There are a few simple rules for the messages:
* The structs has to have the packed attribute
* The first byte of the struct is always the ID of the message, best practice is to #define the IDs and prefil them, as shown in the example messages
* Make sure that the datatypes used in the struct are represented in the same way on both of the platforms you are using. For example, double is usually a 64-bit floating point number, but on some Arduino based board, double is implemented only as a 32-bit number.

# Disclaimer - using \_\_attribute\_\_((\_\_packed\_\_)):
MRS_LLCP messages are all defined with attribute __packed__, which specifies that there should be no padding in the struct.
This is necesary for correct functionlaity of MRS_LLCP.
There is however a risk of undefined behaviour, if you use __packed__ structs incorrectly.
See this example:

```c
struct __attribute__((__packed__)) msg
{
  uint8_t id;
  int     data;
};


struct msg test_msg;
test_msg.data = 4;
int *test = &test_msg.data;
```

On the last line of this example, we assign a pointer to __packed__ struct field, which yields a pointer which is no longer __packed__, and dereferencing such pointer may result in undefined behaviour.
The compiler will only throw a warning if it's configured as such and if it's a recent version of a reasonably smart compiler:
```
Core/Src/main.c:84:12: warning: taking address of packed member of 'struct msg' may result in an unaligned pointer value [-Waddress-of-packed-member]
   84 |   int *test = &test_msg.data;
      |            ^~~~~~~~~~~~~~
```
Also, note that the __attribute(packed)__ is a gcc extension that is only supported by gcc and clang AFAWK, so it will not work at all with e.g. Borland or MSVC.

# using LLCP:
On the side of ROS, we have the [mrs_llcp_ros](https://github.com/ctu-mrs/mrs_llcp_ros) package, which will handle the serial port management for you.
To run LLCP on you low level device:

* Include the library, use extern "C" if your code is in C++
```c
extern "C" {
#include <llcp.h>
}
```

* initialize the llcp_receiver and prepare a bufer for transmitting messages

```c
#define TX_BUFFER_LEN 255
LLCP_Receiver_t llcp_receiver;
uint8_t tx_buffer[TX_BUFFER_LEN];
```

* Sending a message:
  * define the message which you want to send
  * call llcp_prepareMessage() which will fill your TX buffer with the message
  * send out the contents of the buffer over the serial line

```c
  data_msg my_msg; // this is our LLCP message
  uint16_t msg_len;

  // fill the message with data
  my_msg.id = DATA_MSG_ID;
  my_msg.data1_uint8 = my_data1_uint8;
  my_msg.data2_uint32 = my_data2_uint32;
  my_msg.data3_float = my_data3_float;

  //llcp_prepareMessage will fill your TX buffer while returning the number of bytes written
  msg_len = llcp_prepareMessage((uint8_t*)&my_msg, sizeof(my_msg), tx_buffer);

  //send the message out over the serial line. The serial line implementation is based on the platform you are using, the example shown here is from an Arduino
  for (int i = 0; i < msg_len; i++) {
    Serial.write(tx_buffer[i]);
  }
```

* Receiving a message:
  * receive bytes from the serial line byte by byte
  * stick all of the incoming bytes into the llcp_processChar() function
  * the function will return true once a valid LLCP message has been received
  * the LLCP_Message_t pointer will point at the newly received message. The first byte of the message is the message ID, which is used to interpret the message and convert it into the according pre-defined struct.
   
```c
  uint16_t msg_len;
  LLCP_Message_t* llcp_message_ptr;
  
  // This is Arduino Serial line implementation, it will be different on other platforms
  while (Serial.available() > 0) {
    bool checksum_matched;
    uint8_t char_in = Serial.read();

    //individual chars are processed one by one by llcp, if a complete message is received, llcp_processChar() returns true
    if (llcp_processChar(char_in, &llcp_receiver, &llcp_message_ptr, &checksum_matched)) {
      if (checksum_matched) {
        switch (llcp_message_ptr->id) {
          case DATA_MSG_ID: {

              data_msg *received_msg = (data_msg *)llcp_message_ptr;
              /* Do stuff with the received data */
              break;
            }
          case HEARTBEAT_MSG_ID: {

              heartbeat_msg *received_msg = (heartbeat_msg *)llcp_message_ptr;
              /* Do stuff with the received data */
              break;
            }
        }
      }
    }
  }
```
