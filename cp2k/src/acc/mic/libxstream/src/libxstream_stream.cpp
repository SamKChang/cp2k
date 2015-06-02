/******************************************************************************
** Copyright (c) 2014-2015, Intel Corporation                                **
** All rights reserved.                                                      **
**                                                                           **
** Redistribution and use in source and binary forms, with or without        **
** modification, are permitted provided that the following conditions        **
** are met:                                                                  **
** 1. Redistributions of source code must retain the above copyright         **
**    notice, this list of conditions and the following disclaimer.          **
** 2. Redistributions in binary form must reproduce the above copyright      **
**    notice, this list of conditions and the following disclaimer in the    **
**    documentation and/or other materials provided with the distribution.   **
** 3. Neither the name of the copyright holder nor the names of its          **
**    contributors may be used to endorse or promote products derived        **
**    from this software without specific prior written permission.          **
**                                                                           **
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       **
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT         **
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR     **
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT      **
** HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,    **
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED  **
** TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR    **
** PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF    **
** LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING      **
** NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS        **
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.              **
******************************************************************************/
/* Hans Pabst (Intel Corp.)
******************************************************************************/
#if defined(LIBXSTREAM_EXPORTED) || defined(__LIBXSTREAM)
#include "libxstream_stream.hpp"
#include "libxstream_workqueue.hpp"
#include "libxstream_workitem.hpp"
#include "libxstream_event.hpp"

#include <libxstream_begin.h>
#include <algorithm>
#include <string>
#include <cstdio>
#if defined(LIBXSTREAM_STDFEATURES)
# include <atomic>
#endif
#include <libxstream_end.h>

// check whether a signal is really pending; update internal state
//#define LIBXSTREAM_STREAM_CHECK_PENDING


namespace libxstream_stream_internal {

static/*IPO*/ class registry_type {
public:
  typedef libxstream_stream* value_type;

public:
  registry_type()
    : m_istreams(0)
  {
    std::fill_n(m_signals, LIBXSTREAM_MAX_NDEVICES, 0);
    std::fill_n(m_streams, (LIBXSTREAM_MAX_NDEVICES) * (LIBXSTREAM_MAX_NSTREAMS), static_cast<value_type>(0));
  }

  ~registry_type() {
    const size_t n = max_nstreams();
    for (size_t i = 0; i < n; ++i) {
#if defined(LIBXSTREAM_DEBUG)
      if (0 != m_streams[i]) {
        LIBXSTREAM_PRINT(1, "stream=0x%llx (%s) is dangling!", reinterpret_cast<unsigned long long>(m_streams[i]), m_streams[i]->name());
      }
#endif
      libxstream_stream_destroy(m_streams[i]);
    }
  }

public:
  size_t priority_range(int device, int& least, int& greatest) {
    const size_t n = max_nstreams();
    for (size_t i = 0; i < n; ++i) {
      if (const value_type stream = m_streams[i]) {
        const int stream_device = stream->device();
        if (stream_device == device) {
          const int priority = stream->priority();
          least = std::min(least, priority);
          greatest = std::max(least, priority);
        }
      }
    }
    size_t result = 0;
    for (size_t i = 0; i < n; ++i) {
      if (const value_type stream = m_streams[i]) {
        const int stream_device = stream->device();
        if (stream_device == device) {
          const int priority = stream->priority();
          result += priority - greatest;
        }
      }
    }
    return result;
  }

  volatile value_type& allocate() {
#if !defined(LIBXSTREAM_STDFEATURES)
    libxstream_lock *const lock = libxstream_lock_get(this);
    libxstream_lock_acquire(lock);
#endif
    volatile value_type* i = m_streams + LIBXSTREAM_MOD(m_istreams++, (LIBXSTREAM_MAX_NDEVICES) * (LIBXSTREAM_MAX_NSTREAMS));
    while (0 != *i) i = m_streams + LIBXSTREAM_MOD(m_istreams++, (LIBXSTREAM_MAX_NDEVICES) * (LIBXSTREAM_MAX_NSTREAMS));
#if !defined(LIBXSTREAM_STDFEATURES)
    libxstream_lock_release(lock);
#endif
    return *i;
  }

  size_t max_nstreams() const {
    return std::min<size_t>(m_istreams, (LIBXSTREAM_MAX_NDEVICES) * (LIBXSTREAM_MAX_NSTREAMS));
  }

  size_t nstreams(int device, const libxstream_stream* end = 0) const {
    const size_t n = max_nstreams();
    size_t result = 0;
    if (0 == end) {
      for (size_t i = 0; i < n; ++i) {
        const value_type stream = m_streams[i];
        result += (0 != stream && stream->device() == device) ? 1 : 0;
      }
    }
    else {
      for (size_t i = 0; i < n; ++i) {
        const value_type stream = m_streams[i];
        if (end != stream) {
          result += (0 != stream && stream->device() == device) ? 1 : 0;
        }
        else {
          i = n; // break
        }
      }
    }
    return result;
  }

  size_t nstreams() const {
    const size_t n = max_nstreams();
    size_t result = 0;
    for (size_t i = 0; i < n; ++i) {
      const value_type stream = m_streams[i];
      result += 0 != stream ? 1 : 0;
    }
    return result;
  }

  libxstream_signal signal(int device) {
    LIBXSTREAM_ASSERT(-1 <= device && device <= LIBXSTREAM_MAX_NDEVICES);
    return ++m_signals[device+1];
  }

  volatile value_type* streams() {
    return m_streams;
  }

  int enqueue(libxstream_event& event, const libxstream_stream* exclude) {
    int result = LIBXSTREAM_ERROR_NONE;
    const size_t n = max_nstreams();
    bool reset = true;

    for (size_t i = 0; i < n; ++i) {
      const value_type stream = m_streams[i];

      if (stream != exclude) {
        result = event.record(*stream, reset);
        LIBXSTREAM_CHECK_ERROR(result);
        reset = false;
      }
    }

    LIBXSTREAM_ASSERT(LIBXSTREAM_ERROR_NONE == result);
    return result;
  }

  value_type schedule(const libxstream_stream* exclude) {
    const size_t n = max_nstreams();
    size_t start = 0, offset = 0;

    if (0 != exclude) {
      for (size_t i = 0; i < n; ++i) {
        if (m_streams[i] == exclude) {
          start = i;
          offset = 1;
          i = n; // break
        }
      }
    }

    const size_t end = start + n;
    value_type result = 0;

    for (size_t i = start + offset; i < end; ++i) {
      const value_type stream = m_streams[/*i%n*/i<n?i:(i-n)]; // round-robin
      if (0 != stream) {
        result = stream;
        i = end; // break
      }
    }

    return result;
  }

  int wait_all(int device, bool any, bool all) {
    int result = LIBXSTREAM_ERROR_NONE;
    const size_t n = max_nstreams();

    if (0 < n) {
      size_t i = 0;
      do {
        if (const value_type stream = m_streams[i]) {
          const int stream_device = stream->device();
          if (stream_device == device) {
            result = stream->wait(any, all);
            LIBXSTREAM_CHECK_ERROR(result);
          }
        }
        ++i;
      }
      while(i < n);
    }
    else {
      result = wait_all(any, all);
    }

    LIBXSTREAM_ASSERT(LIBXSTREAM_ERROR_NONE == result);
    return result;
  }

  int wait_all(bool any, bool all) {
    int result = LIBXSTREAM_ERROR_NONE;
    const size_t n = max_nstreams();

    if (0 < n) {
      size_t i = 0;
      do {
        if (const value_type stream = m_streams[i]) {
          result = stream->wait(any, all);
          LIBXSTREAM_CHECK_ERROR(result);
        }
        ++i;
      }
      while(i < n);
    }
    else {
      LIBXSTREAM_ASYNC_BEGIN
      {
#if defined(LIBXSTREAM_OFFLOAD) && defined(LIBXSTREAM_ASYNC) && (0 != (2*LIBXSTREAM_ASYNC+1)/2)
        if (0 <= LIBXSTREAM_ASYNC_DEVICE) {
#         pragma offload_wait LIBXSTREAM_ASYNC_TARGET wait(LIBXSTREAM_ASYNC_PENDING)
        }
#endif
      }
      LIBXSTREAM_ASYNC_END(0, LIBXSTREAM_CALL_DEFAULT | LIBXSTREAM_CALL_SYNC | (any ? LIBXSTREAM_CALL_WAIT : 0), work);
      result = any ? work.wait() : work.status();
    }

    LIBXSTREAM_ASSERT(LIBXSTREAM_ERROR_NONE == result);
    return result;
  }

private:
  volatile value_type m_streams[(LIBXSTREAM_MAX_NDEVICES)*(LIBXSTREAM_MAX_NSTREAMS)];
  libxstream_signal m_signals[(LIBXSTREAM_MAX_NDEVICES)+1];
#if defined(LIBXSTREAM_STDFEATURES)
  std::atomic<size_t> m_istreams;
#else
  size_t m_istreams;
#endif
} registry;

} // namespace libxstream_stream_internal


/*static*/int libxstream_stream::priority_range_least()
{
#if defined(LIBXSTREAM_OFFLOAD) && (0 != LIBXSTREAM_OFFLOAD) && defined(LIBXSTREAM_ASYNC) && (2 == (2*LIBXSTREAM_ASYNC+1)/2)
  const int result = LIBXSTREAM_MAX_NTHREADS;
#else // not supported (empty range)
  const int result = 0;
#endif
  return result;
}


/*static*/int libxstream_stream::priority_range_greatest()
{
#if defined(LIBXSTREAM_OFFLOAD) && (0 != LIBXSTREAM_OFFLOAD) && defined(LIBXSTREAM_ASYNC) && (2 == (2*LIBXSTREAM_ASYNC+1)/2)
  const int result = 0;
#else // not supported (empty range)
  const int result = 0;
#endif
  return result;
}


/*static*/int libxstream_stream::enqueue(libxstream_event& event, const libxstream_stream* exclude)
{
  return libxstream_stream_internal::registry.enqueue(event, exclude);
}


/*static*/libxstream_stream* libxstream_stream::schedule(const libxstream_stream* exclude)
{
  return libxstream_stream_internal::registry.schedule(exclude);
}


/*static*/int libxstream_stream::wait_all(int device, bool any, bool all)
{
  return libxstream_stream_internal::registry.wait_all(device, any, all);
}


/*static*/int libxstream_stream::wait_all(bool any, bool all)
{
  return libxstream_stream_internal::registry.wait_all(any, all);
}


libxstream_stream::libxstream_stream(int device, int priority, const char* name)
  : m_retry(0), m_device(device), m_priority(priority), m_thread(-1)
#if defined(LIBXSTREAM_OFFLOAD) && defined(LIBXSTREAM_ASYNC) && (2 == (2*LIBXSTREAM_ASYNC+1)/2)
  , m_handle(0) // lazy creation
  , m_npartitions(0)
#endif
{
  std::fill_n(m_queues, LIBXSTREAM_MAX_NTHREADS, static_cast<libxstream_workqueue*>(0));

  // sanitize the stream priority
  const int priority_least = priority_range_least(), priority_greatest = priority_range_greatest();
  m_priority = std::max(priority_greatest, std::min(priority_least, priority));
#if defined(LIBXSTREAM_TRACE) && ((1 == ((2*LIBXSTREAM_TRACE+1)/2) && defined(LIBXSTREAM_DEBUG)) || 1 < ((2*LIBXSTREAM_TRACE+1)/2))
  if (m_priority != priority) {
    LIBXSTREAM_PRINT(2, "stream priority %i has been clamped to %i", priority, m_priority);
  }
#endif

#if defined(LIBXSTREAM_TRACE) && 0 != ((2*LIBXSTREAM_TRACE+1)/2) && defined(LIBXSTREAM_DEBUG)
  if (name && 0 != *name) {
    const size_t length = std::min(std::char_traits<char>::length(name), sizeof(m_name) - 1);
    std::copy(name, name + length, m_name);
    m_name[length] = 0;
  }
  else {
    m_name[0] = 0;
  }
#else
  libxstream_use_sink(name);
#endif

  using namespace libxstream_stream_internal;
  volatile registry_type::value_type& entry = libxstream_stream_internal::registry.allocate();
  entry = this;
}


libxstream_stream::~libxstream_stream()
{
  LIBXSTREAM_CHECK_CALL_ASSERT(wait(true, true));

  using namespace libxstream_stream_internal;
  volatile registry_type::value_type *const end = registry.streams() + registry.max_nstreams();
  volatile registry_type::value_type *const stream = std::find(registry.streams(), end, this);
  LIBXSTREAM_ASSERT(stream != end);
  *stream = 0; // unregister stream

  const size_t nthreads = nthreads_active();
  for (size_t i = 0; i < nthreads; ++i) {
    delete m_queues[i];
  }

#if defined(LIBXSTREAM_OFFLOAD) && (0 != LIBXSTREAM_OFFLOAD) && !defined(__MIC__) && defined(LIBXSTREAM_ASYNC) && (2 == (2*LIBXSTREAM_ASYNC+1)/2)
  if (0 != m_handle) {
    _Offload_stream_destroy(m_device, m_handle);
  }
#endif
}


libxstream_workqueue::entry_type& libxstream_stream::enqueue(libxstream_workitem& workitem)
{
  LIBXSTREAM_ASSERT(this == workitem.stream());
  const int thread = this_thread_id();
  libxstream_workqueue* q = m_queues[thread];

  if (0 == q) {
    q = new libxstream_workqueue;
    m_queues[thread] = q;
  }

  LIBXSTREAM_ASSERT(0 != q);
  libxstream_workqueue::entry_type& entry = q->allocate_entry();
  entry.push(workitem);
  return entry;
}


libxstream_workqueue* libxstream_stream::queue(size_t retry)
{
  libxstream_workqueue* result = 0 <= m_thread ? m_queues[m_thread] : 0;
  libxstream_workqueue::entry_type *const entry = 0 != result ? result->front() : 0;
  libxstream_workitem *const item = 0 != entry ? entry->item() : 0;
  const bool sync = 0 != item && 0 != (LIBXSTREAM_CALL_SYNC & item->flags());
  size_t max_size = 0;

  if (sync || 0 > m_thread) {
#if 1
    const int nthreads = static_cast<int>(nthreads_active());
    for (int i = 0; i < nthreads; ++i) {
      const int j = i;
#else
    const int nthreads = static_cast<int>(nthreads_active()), end = std::max(m_thread, 0) + nthreads;
    for (int i = m_thread + 1; i < end; ++i) {
      const int j = i < nthreads ? i : (i - nthreads); // round-robin
#endif
      libxstream_workqueue *const q = m_queues[j];
      const libxstream_workqueue::entry_type *const qentry = (0 != q && result != q) ? q->front() : 0;
      const libxstream_workitem *const qitem = 0 != qentry ? qentry->item() : 0;
      const size_t queue_size = 0 != qitem ? q->size() : 0;

      if (max_size < queue_size) {
        max_size = queue_size;
        m_thread = j;
        m_retry = 0;
        result = q;
      }
    }
  }

  if (sync && 0 < max_size) {
    entry->execute();
    entry->pop();
    //m_retry = 0;
  }
  //if ((!sync || 0 == max_size) && 0 < retry && 0 == item) {
  else if (0 < retry && 0 == item) {
    const volatile libxstream_stream_internal::registry_type::value_type *const streams = libxstream_stream_internal::registry.streams();
    const size_t max_nstreams = libxstream_stream_internal::registry.max_nstreams();
    const int nthreads = static_cast<int>(nthreads_active());

    if (m_retry < retry) {
      size_t nstreams = 0, nstreams0 = 0, nstreams1 = 0, nsync = 0;
      for (size_t i = 0; i < max_nstreams; ++i) {
        const libxstream_stream *const stream = streams[i];
        if (0 != stream && this != stream) {
          size_t nqueues = 0;
          for (int j = 0; j < nthreads; ++j) {
            libxstream_workqueue *const q = stream->m_queues[j];
            const libxstream_workqueue::entry_type *const qentry = 0 != q ? q->front() : 0;
            const libxstream_workitem *const qitem = 0 != qentry ? qentry->item() : 0;
            if (0 != qitem) {
              nsync += 0 == (LIBXSTREAM_CALL_SYNC & qitem->flags()) ? 0 : 1;
              ++nqueues;
            }
          }
          nstreams0 += 0 == nqueues ? 1 : 0;
          nstreams1 += 1 == nqueues ? 1 : 0;
          ++nstreams;
        }
      }

      if (nstreams == nstreams0 || 1 >= nstreams1 || 0 < nsync) {
        ++m_retry;
      }
    }
    else {
      m_thread = -1;
      m_retry = 0;
    }
  }

  return result;
}


int libxstream_stream::wait(bool any, bool all)
{
  LIBXSTREAM_ASYNC_BEGIN
  {
    if (!val<const bool,0>()) {
      if (!(LIBXSTREAM_ASYNC_READY)) {
#if defined(LIBXSTREAM_OFFLOAD) && defined(LIBXSTREAM_ASYNC) && (0 != (2*LIBXSTREAM_ASYNC+1)/2)
        if (0 <= LIBXSTREAM_ASYNC_DEVICE) {
#         pragma offload_wait LIBXSTREAM_ASYNC_TARGET_WAIT
        }
#endif
      }
    }
    else { // wait for all thread-local queues
      const int nthreads = val<const int,1>();
      for (int i = 0; i < nthreads; ++i) {
        const libxstream_signal pending = LIBXSTREAM_ASYNC_STREAM->pending(i);
        if (0 != pending) {
#if defined(LIBXSTREAM_OFFLOAD) && defined(LIBXSTREAM_ASYNC) && (0 != (2*LIBXSTREAM_ASYNC+1)/2)
          if (0 <= LIBXSTREAM_ASYNC_DEVICE) {
#           pragma offload_wait LIBXSTREAM_ASYNC_TARGET wait(pending)
          }
#endif
        }
      }
    }
  }
  LIBXSTREAM_ASYNC_END(this, LIBXSTREAM_CALL_DEFAULT | LIBXSTREAM_CALL_SYNC | (any ? LIBXSTREAM_CALL_WAIT : 0), work, all, nthreads_active());

  return work.wait(any);
}


libxstream_signal libxstream_stream::signal() const
{
  return libxstream_stream_internal::registry.signal(m_device);
}


libxstream_signal libxstream_stream::pending(int thread) const
{
  LIBXSTREAM_ASSERT(0 <= thread && thread < LIBXSTREAM_MAX_NTHREADS);
  libxstream_workqueue *const q = m_queues[thread];
  libxstream_signal result = 0;

  if (0 != q) {
    libxstream_workqueue::entry_type *const back = q->back();
    libxstream_workitem *const item = back ? back->item() : 0;
    result = item ? item->pending() : 0;
#if defined(LIBXSTREAM_OFFLOAD) && (0 != LIBXSTREAM_OFFLOAD) && !defined(__MIC__) && defined(LIBXSTREAM_ASYNC) && (0 != (2*LIBXSTREAM_ASYNC+1)/2) && defined(LIBXSTREAM_STREAM_CHECK_PENDING)
    if (0 != result && 0 != _Offload_signaled(m_device, reinterpret_cast<void*>(result))) {
      item->pending(0);
      result = 0;
    }
#endif
  }

  return result;
}


#if defined(LIBXSTREAM_OFFLOAD) && (0 != LIBXSTREAM_OFFLOAD) && defined(LIBXSTREAM_ASYNC) && (2 == (2*LIBXSTREAM_ASYNC+1)/2)
_Offload_stream libxstream_stream::handle() const
{
  const size_t nstreams = libxstream_stream_internal::registry.nstreams(m_device);

  if (nstreams != m_npartitions) {
    if (0 != m_handle) {
      const_cast<libxstream_stream*>(this)->sync(true, 0); // complete pending operations on old stream
      _Offload_stream_destroy(m_device, m_handle);
    }

    const int nthreads_total = omp_get_max_threads_target(TARGET_MIC, m_device) - 4/*reserved core: threads per core*/;
    const int priority_least = priority_range_least(), priority_greatest = priority_range_greatest();
    LIBXSTREAM_ASSERT(priority_greatest <= priority_least);

    int priority_least_device = priority_least, priority_greatest_device = priority_greatest;
    const size_t priority_sum = libxstream_stream_internal::registry.priority_range(m_device, priority_least_device, priority_greatest_device);
    LIBXSTREAM_ASSERT(priority_greatest_device <= priority_least_device && priority_greatest_device <= m_priority);
    const size_t priority_range_device = priority_least_device - priority_greatest_device;
    LIBXSTREAM_ASSERT(priority_sum <= priority_range_device);

    const size_t istream = libxstream_stream_internal::registry.nstreams(m_device, this); // index
    const size_t denominator = 0 == priority_range_device ? nstreams : (priority_range_device - priority_sum);
    const size_t nthreads = (0 == priority_range_device ? nthreads_total : (priority_range_device - (m_priority - priority_greatest_device))) / denominator;
    const size_t remainder = nthreads_total - nthreads * denominator;
    const int ithreads = static_cast<int>(nthreads + (istream < remainder ? 1/*imbalance*/ : 0));

    LIBXSTREAM_PRINT(3, "stream=0x%llx is mapped to %i threads", reinterpret_cast<unsigned long long>(this), ithreads);
    m_handle = _Offload_stream_create(m_device, ithreads);
    m_npartitions = nstreams;
  }

  return m_handle;
}
#endif


const libxstream_stream* cast_to_stream(const void* stream)
{
  return static_cast<const libxstream_stream*>(stream);
}


libxstream_stream* cast_to_stream(void* stream)
{
  return static_cast<libxstream_stream*>(stream);
}


const libxstream_stream* cast_to_stream(const libxstream_stream* stream)
{
  return stream;
}


libxstream_stream* cast_to_stream(libxstream_stream* stream)
{
  return stream;
}


const libxstream_stream* cast_to_stream(const libxstream_stream& stream)
{
  return &stream;
}


libxstream_stream* cast_to_stream(libxstream_stream& stream)
{
  return &stream;
}

#endif // defined(LIBXSTREAM_EXPORTED) || defined(__LIBXSTREAM)
