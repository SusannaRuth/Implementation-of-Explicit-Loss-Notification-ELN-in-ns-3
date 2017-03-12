# Implementation of Explicit Loss Notification (ELN) in ns-3
## Course Code: CS821
## Assignment: #FP4
### Overview:
Explicit Loss Notification (ELN) is a mechanism by which the reason for the loss of a packet can be communicated to the TCP sender. In particular, it provides a way by which senders can be informed that a loss happened because of reasons unrelated to network congestion (e.g., due to wireless bit errors), so that sender retransmissions can be decoupled from congestion control. If the receiver or a base station knows for sure that the loss of a segment was not due to congestion, it sets the ELN bit in the TCP header and propagate it to the source [1]. This has already been implemented in ns-2 [2]. This repository contains the implementation of ELN in ns-3 [3]. 
### References:
[1] Hari Balakrishnan, Randy H. Katz.(1998). Explicit Loss Notification and Wireless Web Performance. Proc. IEEE Globecom Internet Mini-Conference, Sydney, Australia, November 1998.

[2] http://www.isi.edu/nsnam/ns/

[3] http://www.nsnam.org/
