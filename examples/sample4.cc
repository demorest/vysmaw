/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
// Copyright © 2016 Associated Universities, Inc. Washington DC, USA.
//
// This file is part of vysmaw.
//
// vysmaw is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// vysmaw is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// vysmaw.  If not, see <http://www.gnu.org/licenses/>.
//
#include <iostream>
#include <array>
#include <memory>
#include <csignal>
#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <set>
#include <sstream>
#include <string>
#include <ctime>
#include <vysmaw.h>

// max time to wait for message on queue
#define QUEUE_TIMEOUT_MICROSEC 100000

using namespace std;

namespace std {
// default_delete specialization for struct vysmaw_message
template <> struct default_delete<struct vysmaw_message> {
  void operator()(struct vysmaw_message *msg) {
    vysmaw_message_unref(msg);
  }
};

// default_delete specialization for struct vysmaw_configuration
template <> struct default_delete<struct vysmaw_configuration> {
  void operator()(struct vysmaw_configuration *conf) {
    vysmaw_configuration_free(conf);
  }
};
}

// a filter that accepts everything
void
filter(const char *config_id, const uint8_t stations[2],
       uint8_t baseband_index, uint8_t baseband_id,
       uint8_t spectral_window_index, uint8_t polarization_product_id,
       const struct vys_spectrum_info *infos, uint8_t num_infos,
       void *user_data, bool *pass_filter)
{
  unsigned *num_cb = reinterpret_cast<unsigned *>(user_data);
  *num_cb += 1;
  for (auto i = 0; i < num_infos; ++i)
    *pass_filter++ = true;
}

// handle sigint for user exit condition
sig_atomic_t sigint_occurred = false;
void sigint_handler(int p)
{
  sigint_occurred = true;
}

// get a message from a message queue, with a timeout to support user interrupt
// handling
unique_ptr<struct vysmaw_message>
pop(vysmaw_message_queue q)
{
  return unique_ptr<struct vysmaw_message>(
    vysmaw_message_queue_timeout_pop(q, QUEUE_TIMEOUT_MICROSEC));
}

// display counts of messages
void
show_counters(array<unsigned,VYSMAW_MESSAGE_END + 1> &counters)
{
  const unordered_map<enum vysmaw_message_type,string> names = {
    {VYSMAW_MESSAGE_SPECTRA, "spectra-message"},
    {VYSMAW_MESSAGE_QUEUE_ALERT, "message-queue-alert"},
    {VYSMAW_MESSAGE_SPECTRUM_BUFFER_STARVATION, "data-buffer-starvation"},
    {VYSMAW_MESSAGE_SIGNAL_RECEIVE_FAILURE, "signal-receive-failure"},
    {VYSMAW_MESSAGE_VERSION_MISMATCH, "vys-version-mismatch"},
    {VYSMAW_MESSAGE_SIGNAL_RECEIVE_QUEUE_UNDERFLOW, "signal-receive-queue-underflow"},
    {VYSMAW_MESSAGE_END, "end"},
  };

  size_t max_name_len = 0;
  for (auto&& n : names)
    max_name_len = max(n.second.length(), max_name_len);

  const enum vysmaw_message_type msg_types[] = {
    VYSMAW_MESSAGE_SPECTRA,
    VYSMAW_MESSAGE_QUEUE_ALERT,
    VYSMAW_MESSAGE_SPECTRUM_BUFFER_STARVATION,
    VYSMAW_MESSAGE_SIGNAL_RECEIVE_QUEUE_UNDERFLOW,
    VYSMAW_MESSAGE_SIGNAL_RECEIVE_FAILURE,
    VYSMAW_MESSAGE_VERSION_MISMATCH,
    VYSMAW_MESSAGE_END
  };

  for (auto&& m : msg_types) {
    cout.width(max_name_len);
    cout << right << names.at(m);
    cout << ": " << counters[m] << endl;
  }
}

template <typename A>
string
elements(const set<A> &s)
{
  stringstream result;
  for (auto &&a : s)
    result << to_string(a) << " ";
  return result.str();
}

int
main(int argc, char *argv[])
{
  unique_ptr<struct vysmaw_configuration> config;

  typedef chrono::duration<long> sec;
  typedef chrono::duration<long,milli> ms;
  typedef chrono::duration<long,nano> ns;
  ms duration = chrono::duration<long,milli>::max();
  bool timing_run = false;

  // initialize vysmaw configuration
  stringstream usage;
  usage << "usage: "
       << argv[0]
       << " <--timing | -t>"
       << " [config] <duration (ms)>"
       << endl;
  switch (argc) {
  case 4: {
    duration = ms(stoul(argv[3]));
    config.reset(vysmaw_configuration_new(argv[2]));
    string opt = argv[1];
    if (opt != "--timing" && opt != "-t") {
      cerr << usage.str();
      return -1;
    }
    timing_run = true;
    break;
  }
  case 3:
    duration = ms(stoul(argv[2]));
  case 2:
    config.reset(vysmaw_configuration_new(argv[1]));
    break;
  default:
    cerr << usage.str();
    return -1;
  }

  // one consumer, using filter()
  unsigned num_cb = 0;
  struct vysmaw_consumer consumer = {
    .filter = filter,
    .filter_data = &num_cb
  };

  // this application keeps count of the message types it receives
  array<unsigned,VYSMAW_MESSAGE_END + 1> counters;
  counters.fill(0);
  unsigned num_valid_spectra = 0;

  // catch SIGINT to exit gracefully
  bool interrupted = false;
  signal(SIGINT, sigint_handler);

  // a variety of summary accumulators
  set<uint8_t> stations;
  set<uint8_t> bb_indexes;
  set<uint8_t> bb_ids;
  set<uint8_t> spw_indexes;
  set<uint8_t> pp_ids;
  set<uint16_t> num_channels;
  set<uint16_t> num_bins;
  unsigned num_alerts = 0;
  unsigned num_spectrum_buffers_unavailable = 0;
  unsigned num_signal_buffers_unavailable = 0;
  unsigned num_spectra_mismatched_version = 0;
  unsigned num_verification_failures = 0;
  set<string> signal_receive_status;
  set<string> rdma_read_status;
  set<string> config_ids;

  // start vysmaw client
  bool timeout = false;

  // take messages until a VYSMAW_MESSAGE_END appears
  sec report = chrono::duration<long>(1);
  auto t0 = chrono::system_clock::now();
  auto last_report = t0;
  long latest_spectrum_ts = 0;

  vysmaw_handle handle = vysmaw_start(config.get(), &consumer);
  unique_ptr<struct vysmaw_message> message = move(pop(consumer.queue));
#define TCOUNT_MAX 100
  unsigned tcount = TCOUNT_MAX;
  auto t1 = chrono::system_clock::now();
  while ((!message || message->typ != VYSMAW_MESSAGE_END)) {
    // start shutdown if requested by user
    if (sigint_occurred && !interrupted) {
      if (handle) vysmaw_shutdown(handle);
      handle = nullptr;
      interrupted = true;
    }
    if (!timeout) {
      timeout = t1 - t0 > duration;
      if (timeout) {
        if (handle) vysmaw_shutdown(handle);
        handle = nullptr;
        cout << "run time limit exceeded" << endl;
      }
    }
    // record message type and accumulate summary information
    assert(!message || message->typ < VYSMAW_MESSAGE_END);
    if (message) {
      ++counters[message->typ];
      switch (message->typ) {
      case VYSMAW_MESSAGE_SPECTRA: {
        if (!timing_run) {
          struct vysmaw_data_info *info =
            &message->content.spectra.info;
          stations.insert(info->stations[0]);
          stations.insert(info->stations[1]);
          bb_indexes.insert(info->baseband_index);
          bb_ids.insert(info->baseband_id);
          spw_indexes.insert(info->spectral_window_index);
          pp_ids.insert(info->polarization_product_id);
          num_channels.insert(info->num_channels);
          num_bins.insert(info->num_bins);
          config_ids.insert(info->config_id);
        }
        for (unsigned i = 0; i < message->content.spectra.num_spectra; ++i) {
          latest_spectrum_ts = message->data[i].timestamp;
          if (message->data[i].failed_verification)
            ++num_verification_failures;
          else if (message->data[i].rdma_read_status[0] != '\0')
            rdma_read_status.insert(message->data[i].rdma_read_status);
          else
            ++num_valid_spectra;
          // visibilities are at message->data[i].values
        }
        break;
      }
      case VYSMAW_MESSAGE_QUEUE_ALERT:
        ++num_alerts;
        break;
      case VYSMAW_MESSAGE_SPECTRUM_BUFFER_STARVATION:
        num_spectrum_buffers_unavailable +=
          message->content.num_spectrum_buffers_unavailable;
        break;
      case VYSMAW_MESSAGE_VERSION_MISMATCH:
        num_spectra_mismatched_version +=
          message->content.num_spectra_mismatched_version;
        break;
      case VYSMAW_MESSAGE_SIGNAL_RECEIVE_FAILURE:
        signal_receive_status.insert(
          message->content.signal_receive_status);
        break;
      default:
        break;
      }
    }

    // get next message
    message = move(pop(consumer.queue));
    if (timing_run) {
      if (--tcount == 0) {
        tcount = TCOUNT_MAX;
        t1 = chrono::system_clock::now();
        if (t1 - last_report >= report) {
          auto ts = ns(latest_spectrum_ts);
          chrono::time_point<std::chrono::system_clock,ns> tp(ts);
          ns diff(t1 - tp);
          cout << "timestamp diff "
               << diff.count() * ((double)ns::period::num / ns::period::den)
               << endl;
          last_report = t1;
        }
      }
    } else {
      t1 = chrono::system_clock::now();
    }
  }
  if (message) ++counters[message->typ];

  // display counts of received messages
  if (interrupted) cout << endl;
  show_counters(counters);

  // display message for end condition
  if (message) {
    switch (message->content.result.code) {
    case vysmaw_result::VYSMAW_NO_ERROR:
      cout << "ended without error" << endl;
      break;

    case vysmaw_result::VYSMAW_SYSERR:
      cout << "ended with errors" << endl
           << message->content.result.syserr_desc;
      break;

    case vysmaw_result::VYSMAW_ERROR_BUFFPOOL:
      cout << "ended with fatal 'buffpool' error" << endl;
      break;

    default:
      break;
    }
  }

  // performance summary
  auto span =
    chrono::duration_cast<chrono::duration<double> >(t1 - t0);
  cout << to_string(num_cb)
       << " callbacks and "
       << to_string(num_valid_spectra)
       << " valid spectra in "
       << span.count()
       << " seconds ("
       << (num_valid_spectra / span.count())
       << " valid spectra per sec)"
       << endl;

  // error summary...only when it's interesting
  if (num_verification_failures > 0)
    cout << "num verify errors : "
         << num_verification_failures << endl;
  if (num_alerts > 0)
    cout << "num queue alerts  : "
         << num_alerts << endl;
  if (num_spectrum_buffers_unavailable > 0)
    cout << "num data buff miss: "
         << num_spectrum_buffers_unavailable << endl;
  if (num_signal_buffers_unavailable > 0)
    cout << "num sig buff miss : "
         << num_signal_buffers_unavailable << endl;
  if (num_spectra_mismatched_version > 0)
    cout << "num vsn mismatch  : "
         << num_spectra_mismatched_version << endl;
  if (!signal_receive_status.empty()) {
    cout << "signal rcv errs   :";
    for (auto&& s : signal_receive_status)
      cout << endl << " - " << s;
    cout << endl;
  }
  if (!rdma_read_status.empty()) {
    cout << "rdma read errs    :";
    for (auto&& s : rdma_read_status)
      cout << endl << " - " << s;
    cout << endl;
  }

  if (!timing_run) {
    // data buffer summary
    cout << "stations          : "
         << elements(stations) << endl;
    cout << "baseband indexes  : "
         << elements(bb_indexes) << endl;
    cout << "baseband ids      : "
         << elements(bb_ids) << endl;;
    cout << "spw indexes       : "
         << elements(spw_indexes) << endl;
    cout << "pol prod ids      : "
         << elements(pp_ids) << endl;
    cout << "num channels      : "
         << elements(num_channels) << endl;
    cout << "num bins          : "
         << elements(num_bins) << endl;
    cout << "config ids        :";
    for (auto&& s : config_ids)
      cout << endl << " - " << s;
    cout << endl;
  }

  // release the last message and shut down the library if it hasn't already
  // been done
  if (message) message.reset();
  if (handle) vysmaw_shutdown(handle);

  return 0;
}
