// Copyright 2026 Wason Technology, LLC
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

// M6 acceptance: the rosbag2 recorder and player, loaded as components (the
// way rclcpp_components' component container loads them) into this process,
// record a topic and replay it. Run against rmw_intraprocess_cpp and the
// reference rmw_fastrtps_cpp.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ament_index_cpp/get_resource.hpp"
#include "class_loader/class_loader.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/serialization.hpp"
#include "rclcpp_components/node_factory.hpp"
#include "rosbag2_cpp/reader.hpp"
#include "rosbag2_transport/recorder.hpp"
#include "test_msgs/msg/basic_types.hpp"

using test_msgs::msg::BasicTypes;

namespace
{

bool wait_until(
  const std::function<bool()> & pred,
  std::chrono::milliseconds timeout = std::chrono::milliseconds(15000))
{
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
}

BasicTypes make(int32_t i)
{
  BasicTypes m;
  m.int32_value = i;
  m.int64_value = 7 * static_cast<int64_t>(i);
  m.float32_value = 0.25f * static_cast<float>(i);
  return m;
}

/// Loads node components registered in the ament index (resource type
/// rclcpp_components), like rclcpp_components::ComponentManager does.
class ComponentLoader
{
public:
  explicit ComponentLoader(const std::string & package)
  {
    std::string content;
    std::string prefix;
    if (!ament_index_cpp::get_resource("rclcpp_components", package, content, &prefix)) {
      throw std::runtime_error("no rclcpp_components resource for " + package);
    }
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
      auto sep = line.find(';');
      if (sep == std::string::npos) {
        continue;
      }
      std::string library = line.substr(sep + 1);
      while (!library.empty() && (library.back() == '\r' || library.back() == ' ')) {
        library.pop_back();
      }
      std::filesystem::path path(library);
      if (!path.is_absolute()) {
        path = std::filesystem::path(prefix) / path;
      }
      entries_.push_back({line.substr(0, sep), path.string()});
    }
  }

  rclcpp_components::NodeInstanceWrapper create(
    const std::string & class_name, const rclcpp::NodeOptions & options)
  {
    for (const auto & entry : entries_) {
      if (entry.first != class_name) {
        continue;
      }
      const std::string factory_name =
        "rclcpp_components::NodeFactoryTemplate<" + class_name + ">";
      auto factory =
        loader_for(entry.second).createInstance<rclcpp_components::NodeFactory>(factory_name);
      return factory->create_node_instance(options);
    }
    throw std::runtime_error("component not found: " + class_name);
  }

private:
  /// Component libraries stay loaded until the process exits, as in a
  /// component container that keeps its components. Unloading them would
  /// orphan rosidl_typesupport_cpp's per-type library-handle cache, which lives
  /// in the message packages' static storage (LeakSanitizer then reports the
  /// same 40 allocations with rmw_fastrtps_cpp as with this RMW).
  static class_loader::ClassLoader & loader_for(const std::string & library)
  {
    static auto * loaders =
      new std::map<std::string, std::unique_ptr<class_loader::ClassLoader>>();
    std::unique_ptr<class_loader::ClassLoader> & loader = (*loaders)[library];
    if (!loader) {
      loader = std::make_unique<class_loader::ClassLoader>(library);
    }
    return *loader;
  }

  std::vector<std::pair<std::string, std::string>> entries_;
};

class Rosbag2Components : public ::testing::Test
{
protected:
  static void SetUpTestSuite() {rclcpp::init(0, nullptr);}
  static void TearDownTestSuite() {rclcpp::shutdown();}

  void SetUp() override
  {
    const char * rmw = std::getenv("RMW_IMPLEMENTATION");
    bag_uri = std::filesystem::temp_directory_path() /
      ("m6_bag_" + std::string(rmw != nullptr ? rmw : "default") + "_" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(bag_uri);
  }

  void TearDown() override
  {
    std::error_code ec;
    std::filesystem::remove_all(bag_uri, ec);
  }

  std::filesystem::path bag_uri;
};

/// Spins nodes on a background executor for the lifetime of the object.
class Spinner
{
public:
  Spinner() = default;
  ~Spinner() {stop();}

  void add(const rclcpp::node_interfaces::NodeBaseInterface::SharedPtr & node)
  {
    executor_.add_node(node);
  }

  void start()
  {
    thread_ = std::thread([this]() {executor_.spin();});
  }

  void stop()
  {
    if (thread_.joinable()) {
      executor_.cancel();
      thread_.join();
    }
  }

private:
  rclcpp::executors::SingleThreadedExecutor executor_;
  std::thread thread_;
};

}  // namespace

TEST_F(Rosbag2Components, RecordAndReplayInProcess)
{
  const std::string topic = "/m6_rosbag2_topic";
  constexpr int32_t kCount = 5;
  // TRANSIENT_LOCAL so the recorder (and later the player's subscriber) get the
  // samples regardless of discovery timing on the reference RMW.
  const rclcpp::QoS qos = rclcpp::QoS(kCount).reliable().transient_local();
  ComponentLoader loader("rosbag2_transport");

  // --- Record ---
  {
    auto source = std::make_shared<rclcpp::Node>("m6_source");
    auto pub = source->create_publisher<BasicTypes>(topic, qos);
    for (int32_t i = 0; i < kCount; ++i) {
      pub->publish(make(i));
    }

    rclcpp::NodeOptions options;
    options.parameter_overrides(
    {
      rclcpp::Parameter("storage.uri", bag_uri.string()),
      rclcpp::Parameter("storage.storage_id", "mcap"),
      rclcpp::Parameter("record.all_topics", false),
      rclcpp::Parameter("record.topics", std::vector<std::string>{topic}),
      rclcpp::Parameter("record.disable_keyboard_controls", true),
    });
    auto recorder = loader.create("rosbag2_transport::Recorder", options);
    // The factory made a rosbag2_transport::Recorder that is recording. (This
    // call also makes the test link rosbag2_transport; see CMakeLists.txt.)
    auto recorder_node =
      std::static_pointer_cast<rosbag2_transport::Recorder>(recorder.get_node_instance());
    ASSERT_NE(nullptr, recorder_node);
    EXPECT_FALSE(recorder_node->is_paused());
    recorder_node.reset();
    Spinner spinner;
    spinner.add(recorder.get_node_base_interface());
    spinner.start();
    ASSERT_TRUE(wait_until([&]() {return pub->get_subscription_count() == 1u;}));
    // Give the recorder's executor time to take the delivered history.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    spinner.stop();
    // Destroying the recorder closes the bag.
  }

  // --- Check the bag ---
  {
    rosbag2_cpp::Reader reader;
    reader.open(bag_uri.string());
    bool found = false;
    for (const auto & t : reader.get_all_topics_and_types()) {
      if (t.name == topic) {
        found = true;
        EXPECT_EQ("test_msgs/msg/BasicTypes", t.type);
        EXPECT_EQ("cdr", t.serialization_format);
      }
    }
    EXPECT_TRUE(found);
    rclcpp::Serialization<BasicTypes> serialization;
    int32_t n = 0;
    while (reader.has_next()) {
      auto bag_msg = reader.read_next();
      if (bag_msg->topic_name != topic) {
        continue;
      }
      rclcpp::SerializedMessage serialized(*bag_msg->serialized_data);
      BasicTypes m;
      serialization.deserialize_message(&serialized, &m);
      EXPECT_EQ(make(n), m) << n;
      ++n;
    }
    EXPECT_EQ(kCount, n);
  }

  // --- Replay ---
  {
    auto sink = std::make_shared<rclcpp::Node>("m6_sink");
    std::mutex mutex;
    std::vector<BasicTypes> received;
    auto sub = sink->create_subscription<BasicTypes>(
      topic, qos, [&](const BasicTypes & m) {
        std::lock_guard<std::mutex> lock(mutex);
        received.push_back(m);
      });
    Spinner spinner;
    spinner.add(sink->get_node_base_interface());
    spinner.start();

    rclcpp::NodeOptions options;
    options.parameter_overrides(
    {
      rclcpp::Parameter("storage.uri", bag_uri.string()),
      rclcpp::Parameter("play.disable_keyboard_controls", true),
    });
    // The composable player starts playback in its own thread.
    auto player = loader.create("rosbag2_transport::Player", options);
    Spinner player_spinner;
    player_spinner.add(player.get_node_base_interface());
    player_spinner.start();
    EXPECT_TRUE(
      wait_until(
        [&]() {
          std::lock_guard<std::mutex> lock(mutex);
          return received.size() >= static_cast<size_t>(kCount);
        }));
    player_spinner.stop();
    spinner.stop();
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(static_cast<size_t>(kCount), received.size());
    for (int32_t i = 0; i < kCount; ++i) {
      EXPECT_EQ(make(i), received[static_cast<size_t>(i)]) << i;
    }
  }
}
