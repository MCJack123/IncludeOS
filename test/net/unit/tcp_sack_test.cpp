// This file is a part of the IncludeOS unikernel - www.includeos.org
//
// Copyright 2017 Oslo and Akershus University College of Applied Sciences
// and Alfred Bratterud
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <common.cxx>
#include <net/tcp/sack.hpp>

#include <iostream>
std::ostream& operator<< (std::ostream& out, const net::tcp::sack::Entries& ent) {
  for (auto el : ent) {
    out << el << "\n";
  }
  return out;
}

net::tcp::sack::Entries expected(
  net::tcp::sack::Block first = net::tcp::sack::Block(),
  net::tcp::sack::Block second = net::tcp::sack::Block(),
  net::tcp::sack::Block third = net::tcp::sack::Block())
{
  return {{first, second, third}};
}

CASE("Block test")
{
  using namespace net::tcp::sack;
  seq_t start {0};
  seq_t end   {1500};
  Block block{start, end};

  EXPECT(block.size() == 1500);
  EXPECT(not block.empty());
  EXPECT(block.contains(1000));
  EXPECT(not block.contains(2000));

  block.start -= 1000;
  EXPECT(block.size() == 2500);
  EXPECT(block.contains(1000));
  EXPECT(block.contains(-500));

  block.end -= 1500;
  EXPECT(block.size() == 1000);
  EXPECT(not block.contains(1000));
  EXPECT(block.contains(0));
  EXPECT(block.contains(-500));
  EXPECT(block.contains(-1000));
}

CASE("SACK Fixed List implementation [RFC 2018]")
{
  using namespace std;
  using namespace net::tcp::sack;

  // RFC 2018 example
  /*
    Case 2:  The first segment is dropped but the remaining 7 are
    received.

     Upon receiving each of the last seven packets, the data
     receiver will return a TCP ACK segment that acknowledges
     sequence number 5000 and contains a SACK option specifying
     one block of queued data:

         Triggering    ACK      Left Edge   Right Edge
         Segment

         5000         (lost)
         5500         5000     5500       6000
         6000         5000     5500       6500
         6500         5000     5500       7000
         7000         5000     5500       7500
         7500         5000     5500       8000
         8000         5000     5500       8500
         8500         5000     5500       9000
  */
  using Sack_list = List<Fixed_list<9>>;
  Sack_list sack_list;
  Ack_result res;

  // 5000     5500       6000
  res = sack_list.recv_out_of_order(5500, 500);
  EXPECT(res.entries == expected({5500,6000}));

  // 5000     5500       6500
  res = sack_list.recv_out_of_order(6000, 500);
  EXPECT(res.entries == expected({5500,6500}));

  // 5000     5500       7000
  res = sack_list.recv_out_of_order(6500, 500);
  EXPECT(res.entries == expected({5500,7000}));

  // 5000     5500       7500
  res = sack_list.recv_out_of_order(7000,500);
  EXPECT(res.entries == expected({5500,7500}));

  // 5000     5500       8000
  res = sack_list.recv_out_of_order(7500,500);
  EXPECT(res.entries == expected({5500,8000}));

  // 5000     5500       8500
  res = sack_list.recv_out_of_order(8000,500);
  EXPECT(res.entries == expected({5500,8500}));

  // 5000     5500       9000
  res = sack_list.recv_out_of_order(8500,500);
  EXPECT(res.entries == expected({5500, 9000}));

  /*
    Case 3:  The 2nd, 4th, 6th, and 8th (last) segments are
    dropped.

    The data receiver ACKs the first packet normally.  The
    third, fifth, and seventh packets trigger SACK options as
    follows:

        Triggering  ACK    First Block   2nd Block     3rd Block
        Segment            Left   Right  Left   Right  Left   Right
                           Edge   Edge   Edge   Edge   Edge   Edge

        5000       5500
        5500       (lost)
        6000       5500    6000   6500
        6500       (lost)
        7000       5500    7000   7500   6000   6500
        7500       (lost)
        8000       5500    8000   8500   7000   7500   6000   6500
        8500       (lost)
  */
  sack_list = Sack_list();

  // 5500    6000   6500
  res = sack_list.recv_out_of_order(6000, 500);
  EXPECT(res.entries == expected({6000,6500}));

  // 5500    7000   7500   6000   6500
  res = sack_list.recv_out_of_order(7000, 500);
  EXPECT(res.entries == expected({7000,7500}, {6000,6500}));

  // 5500    8000   8500   7000   7500   6000   6500
  res = sack_list.recv_out_of_order(8000, 500);
  EXPECT(res.entries == expected({8000,8500}, {7000,7500}, {6000,6500}));

  /*
    Suppose at this point, the 4th packet is received out of order.
    (This could either be because the data was badly misordered in the
    network, or because the 2nd packet was retransmitted and lost, and
    then the 4th packet was retransmitted). At this point the data
    receiver has only two SACK blocks to report.  The data receiver
    replies with the following Selective Acknowledgment:

        Triggering  ACK    First Block   2nd Block     3rd Block
        Segment            Left   Right  Left   Right  Left   Right
                           Edge   Edge   Edge   Edge   Edge   Edge

        6500       5500    6000   7500   8000   8500
  */

  // 6000   7500   8000   8500
  res = sack_list.recv_out_of_order(6500, 500);

  EXPECT(res.entries == expected({6000,7500}, {8000,8500}));

  /*
    Suppose at this point, the 2nd segment is received.  The data
    receiver then replies with the following Selective Acknowledgment:

        Triggering  ACK    First Block   2nd Block     3rd Block
        Segment            Left   Right  Left   Right  Left   Right
                           Edge   Edge   Edge   Edge   Edge   Edge

        5500       7500    8000   8500
  */
  res = sack_list.new_valid_ack(5500 + 500);

  EXPECT(res.entries == expected({8000,8500}));
  EXPECT(res.bytes == 7500-6000);


  // Create a hole which connects both ends and fill the hole
  sack_list = Sack_list();

  res = sack_list.recv_out_of_order(5500, 500);
  EXPECT(res.entries == expected({5500,6000}));

  res = sack_list.recv_out_of_order(6500, 500);
  EXPECT(res.entries == expected({6500,7000}, {5500,6000}));

  res = sack_list.recv_out_of_order(6000, 500);
  EXPECT(res.entries == expected({5500,7000}));

  res = sack_list.new_valid_ack(5500);
  EXPECT(res.entries == expected());
  EXPECT(res.bytes == 1500);
}

CASE("SACK block list is full")
{

  using namespace net::tcp::sack;

  constexpr int list_size = 9;
  using Sack_list = List<Fixed_list<list_size>>;
  Sack_list sack_list;
  Ack_result res;


  uint32_t seq = 1000;
  int incr = 1000;
  int blksz = 500;
  for (int i = 0; i < list_size; i++){

    // Fill with a 100b hole from previous entry
    res = sack_list.recv_out_of_order(seq, blksz);
    seq += incr;
  }

  // Fully populated sack list
  EXPECT(res.entries == expected({seq - incr, (seq - incr ) +  blksz},
                                 {seq - incr * 2, (seq - incr * 2) + blksz  },
                                 {seq - incr * 3, (seq - incr * 3) + blksz } ));

  EXPECT(res.bytes == blksz);

  // Try adding one more
  res = sack_list.recv_out_of_order(seq, blksz);

  // We should now get the same
  EXPECT(res.entries == expected({seq - incr, (seq - incr ) +  blksz},
                                 {seq - incr * 2, (seq - incr * 2) + blksz  },
                                 {seq - incr * 3, (seq - incr * 3) + blksz } ));

  // Nothing inserted
  EXPECT(res.bytes == 0);

  // Add a block that connects to the end, which should free up one spot
  res = sack_list.recv_out_of_order(seq - incr + blksz, blksz);
  EXPECT(res.bytes == blksz);

  // Last block should now be larger
  EXPECT(res.entries == expected({seq - incr, (seq - incr ) + blksz + blksz },
                                 {seq - incr * 2, (seq - incr * 2) + blksz },
                                 {seq - incr * 3, (seq - incr * 3) + blksz } ));


  // Add a block that connects two blocks, which should free up one spot

}
