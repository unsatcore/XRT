// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2020-2021 Xilinx, Inc. All rights reserved.
// Copyright (C) 2023 Advanced Micro Devices, Inc. All rights reserved.

// This file implements XRT APIs as declared in
// core/include/experimental/xrt_aie.h -- end user APIs
// core/include/experimental/xrt_graph.h -- end user APIs
// core/include/xcl_graph.h -- shim level APIs
#define XCL_DRIVER_DLL_EXPORT  // exporting xrt_graph.h
#define XRT_CORE_COMMON_SOURCE // in same dll as core_common
#include "core/include/xrt/xrt_graph.h"
#include "core/include/xrt/xrt_aie.h"
#include "core/include/xrt/xrt_bo.h"
#include "core/include/xcl_graph.h"

#include "core/include/experimental/xrt_device.h"
#include "core/common/api/device_int.h"
#include "core/common/api/native_profile.h"
#include "core/common/device.h"
#include "core/common/error.h"
#include "core/common/message.h"
#include "core/common/system.h"

#include <limits>
#include <memory>

namespace xrt {

class graph_impl
{
private:
  std::shared_ptr<xrt_core::device> device;
  xclGraphHandle handle;

public:
  graph_impl(std::shared_ptr<xrt_core::device> dev, xclGraphHandle ghdl)
    : device(std::move(dev))
    , handle(ghdl)
  {}

  ~graph_impl()
  {
    device->close_graph(handle);
  }

  xclGraphHandle
  get_handle() const
  {
    return handle;
  }

  void
  reset()
  {
    device->reset_graph(handle);
  }

  uint64_t
  get_timestamp()
  {
    return (device->get_timestamp(handle));
  }

  void
  run(int iterations)
  {
    device->run_graph(handle, iterations);
  }

  int
  wait(int timeout)
  {
    return (device->wait_graph_done(handle, timeout));
  }

  void
  wait(uint64_t cycle)
  {
    device->wait_graph(handle, cycle);
  }

  void
  suspend()
  {
    device->suspend_graph(handle);
  }

  void
  resume()
  {
    device->resume_graph(handle);
  }

  void
  end(uint64_t cycle)
  {
    device->end_graph(handle, cycle);
  }

  void
  update_rtp(const char* port, const char* buffer, size_t size)
  {
    device->update_graph_rtp(handle, port, buffer, size);
  }

  void
  read_rtp(const char* port, char* buffer, size_t size)
  {
    device->read_graph_rtp(handle, port, buffer, size);
  }
};

}

namespace xrt { namespace aie {

class profiling_impl
{
private:
  std::shared_ptr<xrt_core::device> device;
  int profiling_hdl;

public:
  using handle = int;
  static constexpr handle invalid_handle = -1;

  profiling_impl(std::shared_ptr<xrt_core::device> dev)
    : device(std::move(dev)),
      profiling_hdl(invalid_handle)
  {}

  ~profiling_impl()
  {
    try {
      device->stop_profiling(profiling_hdl);
    }
    catch(...) {
      // do nothing
    }
  }

  handle 
  start_profiling(int option, const std::string& port1_name, const std::string& port2_name, uint32_t value)
  {
    profiling_hdl = device->start_profiling(option, port1_name.c_str(), port2_name.c_str(), value);
    return profiling_hdl;
  }

  uint64_t
  read_profiling()
  {
    if (profiling_hdl == invalid_handle)
      throw xrt_core::error(-EINVAL, "Not a valid profiling handle");

    return device->read_profiling(profiling_hdl);
    
  }

  void
  stop_profiling()
  {
    if (profiling_hdl == invalid_handle)
      throw xrt_core::error(-EINVAL, "Not a valid profiling handle");

    device->stop_profiling(profiling_hdl);
    profiling_hdl = invalid_handle;
  }

};

}}

namespace {

// C-API Graph handles are inserted to this map.
// Note: xrtGraphClose must be explicitly called before xclClose.
static std::map<xrtGraphHandle, std::shared_ptr<xrt::graph_impl>> graph_cache;

static std::shared_ptr<xrt::graph_impl>
open_graph(xrtDeviceHandle dhdl, const xuid_t xclbin_uuid, const char* graph_name, xrt::graph::access_mode am)
{
  auto core_device = xrt_core::device_int::get_core_device(dhdl);
  auto handle = core_device->open_graph(xclbin_uuid, graph_name, am);
  auto ghdl = std::make_shared<xrt::graph_impl>(core_device, handle);
  return ghdl;
}

static std::shared_ptr<xrt::graph_impl>
open_graph(const xrt::device& device, const xrt::uuid& xclbin_id, const std::string& name, xrt::graph::access_mode am)
{
  auto core_device = device.get_handle();
  auto handle = core_device->open_graph(xclbin_id.get(), name.c_str(), am);
  auto ghdl = std::make_shared<xrt::graph_impl>(core_device, handle);
  return ghdl;
}

static std::shared_ptr<xrt::graph_impl>
get_graph_hdl(xrtGraphHandle graph_handle)
{
  auto itr = graph_cache.find(graph_handle);
  if (itr == graph_cache.end())
    throw xrt_core::error(-EINVAL, "No such graph handle");
  return (*itr).second;
}

static void
close_graph(xrtGraphHandle hdl)
{
  if (graph_cache.erase(hdl) == 0)
    throw std::runtime_error("Unexpected internal error");
}

static void
open_aie_context(xrtDeviceHandle dhdl, xrt::aie::access_mode am)
{
  auto device = xrt_core::device_int::get_core_device(dhdl);
  device->open_aie_context(am);
}

static void
sync_aie_bo(xrtDeviceHandle dhdl, xrtBufferHandle bohdl, const char *gmio_name, xclBOSyncDirection dir, size_t size, size_t offset)
{
  auto device = xrt_core::device_int::get_core_device(dhdl);
  auto bo = xrt::bo(bohdl);
  device->sync_aie_bo(bo, gmio_name, dir, size, offset);
}

static void
reset_aie(xrtDeviceHandle dhdl)
{
  auto device = xrt_core::device_int::get_core_device(dhdl);
  device->reset_aie();
}

static void
sync_aie_bo_nb(xrtDeviceHandle dhdl, xrtBufferHandle bohdl, const char *gmio_name, xclBOSyncDirection dir, size_t size, size_t offset)
{
  auto device = xrt_core::device_int::get_core_device(dhdl);
  auto bo = xrt::bo(bohdl);
  device->sync_aie_bo_nb(bo, gmio_name, dir, size, offset);
}

static void
wait_gmio(xrtDeviceHandle dhdl, const char *gmio_name)
{
  auto device = xrt_core::device_int::get_core_device(dhdl);
  device->wait_gmio(gmio_name);
}

// C-API Profiling handles are inserted to this map.
static std::map<int, std::shared_ptr<xrt::aie::profiling_impl>> profiling_cache;

static std::shared_ptr<xrt::aie::profiling_impl>
create_profiling_event(xrtDeviceHandle dhdl)
{
  auto core_device = xrt_core::device_int::get_core_device(dhdl);
  auto phdl = std::make_shared<xrt::aie::profiling_impl>(core_device);
  return phdl;
}

static std::shared_ptr<xrt::aie::profiling_impl>
create_profiling_event(const xrt::device& device)
{
  auto core_device = device.get_handle();
  auto phdl = std::make_shared<xrt::aie::profiling_impl>(core_device);
  return phdl;
}

inline void
send_exception_message(const char* msg)
{
  xrt_core::message::send(xrt_core::message::severity_level::error, "XRT", msg);
}

}

//////////////////////////////////////////////////////////////
// xrt_graph C++ API implementations (xrt_graph.h)
//////////////////////////////////////////////////////////////
namespace xrt {

graph::
graph(const xrt::device& device, const xrt::uuid& xclbin_id, const std::string& name, graph::access_mode am)
  : handle(open_graph(device, xclbin_id, name, am))
{}

void
graph::
reset() const
{
  xdp::native::profiling_wrapper("xrt::graph::reset", [=]{
    handle->reset();
  });
}

uint64_t
graph::
get_timestamp() const
{
  return xdp::native::profiling_wrapper("xrt::graph::get_timestamp", [=]{return (handle->get_timestamp());});
}

void
graph::
run(uint32_t iterations)
{
  xdp::native::profiling_wrapper("xrt::graph::run", [=]{
    handle->run(iterations);
  });
}

void
graph::
wait(std::chrono::milliseconds timeout_ms)
{
  xdp::native::profiling_wrapper("xrt::graph::wait", [=]{
    if (timeout_ms.count() == 0)
      handle->wait(static_cast<uint64_t>(0));
    else
      handle->wait(static_cast<int>(timeout_ms.count()));
  });
}

void
graph::
wait(uint64_t cycles)
{
  xdp::native::profiling_wrapper("xrt::graph::wait", [=]{
    handle->wait(cycles);
  });
}

void
graph::
suspend()
{
  xdp::native::profiling_wrapper("xrt::graph::suspend", [=]{
    handle->suspend();
  });
}

void
graph::
resume()
{
  xdp::native::profiling_wrapper("xrt::graph::resume", [=]{
    handle->resume();
  });
}

void
graph::
end(uint64_t cycles)
{
  xdp::native::profiling_wrapper("xrt::graph::end", [=]{
    handle->end(cycles);
  });
}

void
graph::
update_port(const std::string& port_name, const void* value, size_t bytes)
{
  xdp::native::profiling_wrapper("xrt::graph::update_port", [=]{
    handle->update_rtp(port_name.c_str(), reinterpret_cast<const char*>(value), bytes);
  });
}

void
graph::
read_port(const std::string& port_name, void* value, size_t bytes)
{
  xdp::native::profiling_wrapper("xrt::graph::read_port", [=]{
    handle->read_rtp(port_name.c_str(), reinterpret_cast<char *>(value), bytes);
  });
}

} // namespace xrt

////////////////////////////////////////////////////////////////
// xrt_aie_profiling C++ API implmentations (xrt_aie.h)
////////////////////////////////////////////////////////////////
namespace xrt { namespace aie {

profiling::
profiling(const xrt::device& device)
  : detail::pimpl<profiling_impl>(std::move(create_profiling_event(device)))
{}

int 
profiling::
start(xrt::aie::profiling::profiling_option option, const std::string& port1_name, const std::string& port2_name, uint32_t value) const
{
  int opt = static_cast<int>(option);
  return xdp::native::profiling_wrapper("xrt::aie::profiling::start", [this, opt, &port1_name, &port2_name, value] {
    return get_handle()->start_profiling(opt, port1_name, port2_name, value);
  });
}

uint64_t
profiling::
read() const
{
  return xdp::native::profiling_wrapper("xrt::aie::profiling::read", [this] {
    return get_handle()->read_profiling();
  });
}

void
profiling::
stop() const
{
  xdp::native::profiling_wrapper("xrt::aie::profiling::stop", [this] {
    return get_handle()->stop_profiling();
  });
}

}} //namespace aie, xrt

////////////////////////////////////////////////////////////////
// xrt_aie API implementations (xrt_aie.h, xrt_graph.h)
////////////////////////////////////////////////////////////////

xrtGraphHandle
xrtGraphOpen(xrtDeviceHandle dev_handle, const xuid_t xclbin_uuid, const char* graph_name)
{
  try {
    auto hdl = open_graph(dev_handle, xclbin_uuid, graph_name, xrt::graph::access_mode::primary);
    graph_cache[hdl.get()] = hdl;
    return hdl.get();
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    xrt_core::send_exception_message(ex.what());
  }
  return XRT_NULL_HANDLE;
}

xrtGraphHandle
xrtGraphOpenExclusive(xrtDeviceHandle dev_handle, const xuid_t xclbin_uuid, const char* graph_name)
{
  try {
    auto hdl = open_graph(dev_handle, xclbin_uuid, graph_name, xrt::graph::access_mode::exclusive);
    graph_cache[hdl.get()] = hdl;
    return hdl.get();
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    xrt_core::send_exception_message(ex.what());
  }
  return XRT_NULL_HANDLE;
}

xrtGraphHandle
xrtGraphOpenShared(xrtDeviceHandle dev_handle, const xuid_t xclbin_uuid, const char* graph_name)
{
  try {
    auto hdl = open_graph(dev_handle, xclbin_uuid, graph_name, xrt::graph::access_mode::shared);
    graph_cache[hdl.get()] = hdl;
    return hdl.get();
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    xrt_core::send_exception_message(ex.what());
  }
  return XRT_NULL_HANDLE;
}

void
xrtGraphClose(xrtGraphHandle graph_hdl)
{
  try {
    close_graph(graph_hdl);
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
}

int
xrtGraphReset(xrtGraphHandle graph_hdl)
{
  try {
    auto hdl = get_graph_hdl(graph_hdl);
    hdl->reset();
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

uint64_t
xrtGraphTimeStamp(xrtGraphHandle graph_hdl)
{
  try {
    auto hdl = get_graph_hdl(graph_hdl);
    return hdl->get_timestamp();
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return std::numeric_limits<uint64_t>::max();
}

int
xrtGraphRun(xrtGraphHandle graph_hdl, int iterations)
{
  try {
    auto hdl = get_graph_hdl(graph_hdl);
    hdl->run(iterations);
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

int
xrtGraphWaitDone(xrtGraphHandle graph_hdl, int timeoutMilliSec)
{
  try {
    auto hdl = get_graph_hdl(graph_hdl);
    return hdl->wait(timeoutMilliSec);
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

int
xrtGraphWait(xrtGraphHandle graph_hdl, uint64_t cycle)
{
  try {
    auto hdl = get_graph_hdl(graph_hdl);
    hdl->wait(cycle);
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

int
xrtGraphSuspend(xrtGraphHandle graph_hdl)
{
  try {
    auto hdl = get_graph_hdl(graph_hdl);
    hdl->suspend();
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

int
xrtGraphResume(xrtGraphHandle graph_hdl)
{
  try {
    auto hdl = get_graph_hdl(graph_hdl);
    hdl->resume();
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

int
xrtGraphEnd(xrtGraphHandle graph_hdl, uint64_t cycle)
{
  try {
    auto hdl = get_graph_hdl(graph_hdl);
    hdl->end(cycle);
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

int
xrtGraphUpdateRTP(xrtGraphHandle graph_hdl, const char* port, const char* buffer, size_t size)
{
  try {
    auto hdl = get_graph_hdl(graph_hdl);
    hdl->update_rtp(port, buffer, size);
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

int
xrtGraphReadRTP(xrtGraphHandle graph_hdl, const char* port, char* buffer, size_t size)
{
  try {
    auto hdl = get_graph_hdl(graph_hdl);
    hdl->read_rtp(port, buffer, size);
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

xrtDeviceHandle
xrtAIEDeviceOpen(unsigned int index)
{
  try {
    auto handle = xrtDeviceOpen(index);
    open_aie_context(handle, xrt::aie::access_mode::primary);
    return handle;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return nullptr;
}

xrtDeviceHandle
xrtAIEDeviceOpenExclusive(unsigned int index)
{
  try {
    auto handle = xrtDeviceOpen(index);
    open_aie_context(handle, xrt::aie::access_mode::exclusive);
    return handle;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return nullptr;
}

xrtDeviceHandle
xrtAIEDeviceOpenShared(unsigned int index)
{
  try {
    auto handle = xrtDeviceOpen(index);
    open_aie_context(handle, xrt::aie::access_mode::shared);
    return handle;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return nullptr;
}

int
xrtAIESyncBO(xrtDeviceHandle handle, xrtBufferHandle bohdl, const char *gmioName, enum xclBOSyncDirection dir, size_t size, size_t offset)
{
   return xrtSyncBOAIE(handle, bohdl, gmioName, dir, size, offset);
}

int
xrtSyncBOAIE(xrtDeviceHandle handle, xrtBufferHandle bohdl, const char *gmioName, enum xclBOSyncDirection dir, size_t size, size_t offset)
{
  try {
    sync_aie_bo(handle, bohdl, gmioName, dir, size, offset);
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

int
xrtAIEResetArray(xrtDeviceHandle handle)
{
  return xrtResetAIEArray(handle);
}

int
xrtResetAIEArray(xrtDeviceHandle handle)
{
  try {
    reset_aie(handle);
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

////////////////////////////////////////////////////////////////
// Exposed for Cardano as extensions to xrt_aie.h
////////////////////////////////////////////////////////////////
/**
 * xrtSyncBOAIENB() - Transfer data between DDR and Shim DMA channel
 *
 * @handle:          Handle to the device
 * @bohdl:           BO handle.
 * @gmioName:        GMIO port name
 * @dir:             GM to AIE or AIE to GM
 * @size:            Size of data to synchronize
 * @offset:          Offset within the BO
 *
 * Return:          0 on success, or appropriate error number.
 *
 * Synchronize the buffer contents between GMIO and AIE.
 * Note: Upon return, the synchronization is submitted or error out
 */
int
xrtSyncBOAIENB(xrtDeviceHandle handle, xrtBufferHandle bohdl, const char *gmioName, enum xclBOSyncDirection dir, size_t size, size_t offset)
{
  try {
    sync_aie_bo_nb(handle, bohdl, gmioName, dir, size, offset);
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

/**
 * xrtGMIOWait() - Wait a shim DMA channel to be idle for a given GMIO port
 *
 * @handle:          Handle to the device
 * @gmioName:        GMIO port name
 *
 * Return:          0 on success, or appropriate error number.
 */
int
xrtGMIOWait(xrtDeviceHandle handle, const char *gmioName)
{
  try {
    wait_gmio(handle, gmioName);
    return 0;
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

/**
 * xrtAIEStartProfiling() - Start AIE performance profiling
 *
 * @handle:          Handle to the device
 * @option:          Profiling option.
 * @port1Name:       Profiling port 1 name
 * @port2Name:       Profiling port 2 name
 * @value:           The number of bytes to trigger the profiling event
 *
 * Return:         An integer profiling handle on success,
 *                 or appropriate error number.
 *
 * This function configures the performance counters in AI Engine by given
 * port names and value. The port names and value will have different
 * meanings on different options.
 *
 * Note: Currently, the only supported io profiling option is
 *       io_stream_running_event_count (GMIO and PLIO)
 */
int
xrtAIEStartProfiling(xrtDeviceHandle handle, int option, const char *port1Name, const char *port2Name, uint32_t value)
{
  try {
    auto event = create_profiling_event(handle);
    if (option < 0 || option > 3)
      throw xrt_core::error(-EINVAL, "Not a valid profiling option");
    auto hdl = event->start_profiling(option, port1Name, port2Name, value);
    if (hdl != xrt::aie::profiling_impl::invalid_handle) {
      profiling_cache[hdl] = event;
      return hdl;
    }
    else
      throw xrt_core::error(-EINVAL, "Not a valid profiling handle");
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return -1;
}

/**
 * xrtAIEReadProfiling() - Read the current performance counter value
 *                         associated with the profiling handle.
 *
 * @handle:          Handle to the device
 * @pHandle:         Profiling handle.
 *
 * Return:         The performance counter value, or appropriate error number.
 */
uint64_t
xrtAIEReadProfiling(xrtDeviceHandle /*handle*/, int pHandle)
{
  try {
    auto it = profiling_cache.find(pHandle);
    if (it != profiling_cache.end())
      return it->second->read_profiling();
    else
      throw xrt_core::error(-EINVAL, "No such profiling handle"); 
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
  return std::numeric_limits<uint64_t>::max();
}

/**
 * xrtAIEStopProfiling() - Stop the current performance profiling
 *                         associated with the profiling handle and
 *                         release the corresponding hardware resources.
 *
 * @handle:          Handle to the device
 * @pHandle:         Profiling handle.
 *
 * Return:         0 on success, or appropriate error number.
 */
void
xrtAIEStopProfiling(xrtDeviceHandle /*handle*/, int pHandle)
{
  try {
    auto it = profiling_cache.find(pHandle);
    if (it != profiling_cache.end()) {
      it->second->stop_profiling();
      profiling_cache.erase(pHandle);
    }
    else
      throw xrt_core::error(-EINVAL, "No such profiling handle");   
  }
  catch (const xrt_core::error& ex) {
    xrt_core::send_exception_message(ex.what());
    errno = ex.get_code();
  }
  catch (const std::exception& ex) {
    send_exception_message(ex.what());
  }
}
