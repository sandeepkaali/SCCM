/*
  The MIT License (MIT)
  Copyright (c) 2017 Riccardo Polvara

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  #MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
  EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  #CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
  OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  #SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

  Main class for the deep reinforced landing node.
*/
#include "../include/boundingBox.h"
#include "../include/utilities.h"
#include "ardrone_autonomy/Navdata.h"
#include "gazebo_msgs/GetModelState.h"
#include "gazebo_msgs/ModelState.h"
#include "gazebo_msgs/SetModelState.h"
#include "geometry_msgs/Pose.h"
#include "geometry_msgs/Twist.h"
#include "ros/node_handle.h"
#include "ros/ros.h"
#include "ros/service_client.h"
#include "sensor_msgs/Image.h"
#include "std_msgs/Empty.h"
#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <map>
#include <math.h>
#include <std_srvs/Empty.h>
#include <stdlib.h>
#include <string>
#include <tf/LinearMath/Matrix3x3.h>
#include <tf/transform_datatypes.h>

#include "deep_reinforced_landing/GetCameraImage.h"
#include "deep_reinforced_landing/GetDoneAndReward.h"
#include "deep_reinforced_landing/GetRelativePose.h"
#include "deep_reinforced_landing/NewCameraService.h"
#include "deep_reinforced_landing/ResetPosition.h"
#include "deep_reinforced_landing/SendCommand.h"

const int LANDED_STATUS = 2;

using namespace std;
using namespace cv;

// Node class for deep reinforced landing
class DeepReinforcedLandingUAV {
private:
  ros::NodeHandle nh_;
  // Create a subscriber for getting the UAV's latests status
  ros::Subscriber uav_sub_;
  // Subscribe to the bottom camera's topic
  ros::Subscriber camera_sub_;
  // Publishers for moving the UAV
  ros::Publisher cmd_pub_;
  ros::Publisher land_pub_;
  ros::Publisher takeoff_pub_;
  // Publisher for resetting UAV's pose
  ros::Publisher reset_model_pub_;
  // Publisher for a greyscale/resized image
  ros::Publisher greyscale_camera_pub_;

  // Create a client for getting quadrotor and marker's positions
  ros::ServiceClient get_state_client_;

  // Create a service for offering the done and reward
  ros::ServiceServer service_done_reward_;
  // Create a service for getting the quadrotor pose wrt the markers' one
  ros::ServiceServer service_relative_pose_;
  // Create a service for offering the full camera's image or only the matrix
  ros::ServiceServer service_camera_;
  ros::ServiceServer service_camera_matrix_;
  // Create a service to invoke control's publisher (cmd_pub_, land_pub_,
  // takeoff_pub_)
  ros::ServiceServer service_send_command_;
  // Create a service for getting the reset request...
  ros::ServiceServer service_reset_;
  // ...and then call the service offered by gazebo
  ros::ServiceClient set_state_client_;

  //--------Callbacks and Services-----
  /*
    Set check if the UAV landed

    @param msg is the msg containing the status and sensor's data of the UAV
  */
  void setDoneCallback(const ardrone_autonomy::Navdata &msg);

  /*
    Get camera's latest frame

    @param msg is the latest frame acquired by the camera
  */
  void getImageCallback(const sensor_msgs::Image &msg);

  /*
    Get UAV's pose

    @param req is an empty message
    @param res contains the UAV status and the reward
  */
  bool getStatus(deep_reinforced_landing::GetDoneAndReward::Request &req,
                 deep_reinforced_landing::GetDoneAndReward::Response &res);

  /*
    Get camera's image as sensor_msgs/Image data type

    @param req is an empty message
    @param contains the latest frame from teh camera with various info (size,
    encoding etc)
  */
  bool getCameraImage(deep_reinforced_landing::GetCameraImage::Request &req,
                      deep_reinforced_landing::GetCameraImage::Response &res);

  /*
    Get camera's image matrix only

    @param req is an empty message
    @param res is the latest frame acquired by the camera after being scaled and
    greyscale converted
  */
  bool getNewCamera(deep_reinforced_landing::NewCameraService::Request &req,
                    deep_reinforced_landing::NewCameraService::Response &res);
  /*
    Set new UAV's pose

    @param req is a boolean value that set to true reset the UAV in a random
    pose inside the BB_flight
    @param res is an empty message
  */
  bool setModelState(deep_reinforced_landing::ResetPosition::Request &req,
                     deep_reinforced_landing::ResetPosition::Response &res);

  /*
    Send a new command to the UAV

    @param req is a string representing the command to send to the UAV
    (left,right, ascend, descend, forward,backward, rotate_left, rotate_right,
    takeoff, land
    @param res is an empty message
  */
  bool sendCommand(deep_reinforced_landing::SendCommand::Request &req,
                   deep_reinforced_landing::SendCommand::Response &res);

  bool getRelativePose(deep_reinforced_landing::GetRelativePose::Request &req,
                       deep_reinforced_landing::GetRelativePose::Response &res);

  void setActionCommand(std::string action);

  //-------Data-----------
  // Server for getting UAV's pose and various related variables
  gazebo_msgs::GetModelState srv_;
  geometry_msgs::Pose quadrotorPose_, markerPose_, quadrotor_to_marker_pose_;
  geometry_msgs::Pose start_pose_;
  geometry_msgs::Twist start_twist_;
  gazebo_msgs::SetModelState set_model_state_;

  // Half side for the landing BB and the flight one
  double bb_landing_half_size_, bb_flight_half_size_;
  double bb_landing_height_, bb_flight_height_;
  BoundingBox bb_landing_, bb_flight_;
  double respawn_height;

  // Reinforcement Learning data
  bool done_;
  float reward_;
  bool reset_;
  std::string action_;

  bool wrong_altitude_;
  float altitude_;

  // Image related variables
  sensor_msgs::Image image_total_;
  cv::Mat src_;
  cv::Mat out_;

  // UAV's flight control related variables
  geometry_msgs::Twist velocity_cmd_;
  std_msgs::Empty land_takeoff_cmd_;
  bool can_takeoff_, can_land_, can_move_;

  // Offers useful methods
  Utilities utilities_;

protected:
public:
  DeepReinforcedLandingUAV();
  ~DeepReinforcedLandingUAV();

  bool getReset();
  void setReset(bool reset);
  /*
    Generate a random pose for the UAV inside the bound box limits.

    @return the pose of the UAV expressed as position (x,y,z) and orientation
    (x,y,z,w)
  */
  gazebo_msgs::SetModelState getModelState();
  /*
    Assign the UAV to a new pose in the world

    @param set_model_state is the new pose expressed as position (x,y,z) and
    orientatio (x,y,z,w)
  */
  void setModelState(gazebo_msgs::SetModelState set_model_state);

  bool getCanMove();
  bool getCanTakeOff();
  bool getCanLand();
  void setCanMove(bool can_move);
  void setCanTakeOff(bool can_takeoff);
  void setCanLand(bool can_land);
  ros::Publisher getLandPub();
  ros::Publisher getTakeoffPub();
  ros::Publisher getCmdPub();
  geometry_msgs::Twist getVelocityCmd();
  int getReward();
  void setReward(double reward);
  void setReward();
};

DeepReinforcedLandingUAV::DeepReinforcedLandingUAV() {
  camera_sub_ = nh_.subscribe("ardrone/bottom/image_raw", 1,
                              &DeepReinforcedLandingUAV::getImageCallback, this);
  cmd_pub_ = nh_.advertise<geometry_msgs::Twist>("/cmd_vel", 1);
  land_pub_ = nh_.advertise<std_msgs::Empty>("/ardrone/land", 1);
  takeoff_pub_ = nh_.advertise<std_msgs::Empty>("/ardrone/takeoff", 1);
  reset_model_pub_ =
      nh_.advertise<gazebo_msgs::ModelState>("/gazebo/set_model_state", 1);
  greyscale_camera_pub_ =
      nh_.advertise<sensor_msgs::Image>("/drl/grey_camera", 1);

  get_state_client_ =
      nh_.serviceClient<gazebo_msgs::GetModelState>("/gazebo/get_model_state");
  service_done_reward_ = nh_.advertiseService(
      "drl/get_done_reward", &DeepReinforcedLandingUAV::getStatus, this);
  service_camera_ = nh_.advertiseService(
      "drl/get_camera_image", &DeepReinforcedLandingUAV::getCameraImage, this);
  service_camera_matrix_ =
      nh_.advertiseService("drl/get_camera_image_matrix",
                           &DeepReinforcedLandingUAV::getNewCamera, this);
  service_reset_ = nh_.advertiseService(
      "drl/set_model_state", &DeepReinforcedLandingUAV::setModelState, this);
  set_state_client_ =
      nh_.serviceClient<gazebo_msgs::SetModelState>("/gazebo/set_model_state");
  service_send_command_ = nh_.advertiseService(
      "drl/send_command", &DeepReinforcedLandingUAV::sendCommand, this);
  service_relative_pose_ = nh_.advertiseService(
      "drl/get_relative_pose", &DeepReinforcedLandingUAV::getRelativePose, this);

  // With a flight BB having 15m per side, we need a minimum height of 20m for
  // perceiving the marker
  bb_flight_half_size_ = 1.5;
  bb_flight_height_ = 20.0;
  respawn_height = 15.0; // the height at which the UAV must be respawn
  double bb_flight_volume =
      pow(2 * bb_flight_half_size_, 2) * bb_flight_height_;
  // The volume of the landing BB is 1/10 of the flight BB's one
  double bb_landing_volume = (bb_flight_volume / 10.0) * 2;
  bb_landing_height_ = 1.5;
  // Calculate the side of the landing BB's base and divide it by two
  // bb_landing_half_size_ = sqrt(bb_landing_volume / bb_flight_height_) / 2;
  bb_landing_half_size_ = 0.75; // add math expression

  done_ = false;
  reward_ = 0;
  reset_ = false;
  can_takeoff_ = false;
  can_land_ = false;
  can_move_ = false;

  wrong_altitude_ = false;

  //----------- RESET POSE -----
  start_pose_.position.x = start_pose_.position.y = start_pose_.position.z = 0;
  start_pose_.orientation.x = start_pose_.orientation.y =
      start_pose_.orientation.z = start_pose_.orientation.w = 0;
  start_twist_.linear.x = start_twist_.linear.y = start_twist_.linear.z =
      start_twist_.angular.x = start_twist_.angular.y = start_twist_.angular.z =
          0.0;

  gazebo_msgs::ModelState model_state;
  model_state.model_name = (std::string) "quadrotor";
  model_state.reference_frame = (std::string) "world";
  model_state.pose = start_pose_;
  model_state.twist = start_twist_;

  set_model_state_.request.model_state = model_state;
  //----------------------------

  if (reset_ == true) {
    set_state_client_.call(set_model_state_);
  }
  reset_ = false;
}

DeepReinforcedLandingUAV::~DeepReinforcedLandingUAV() {}

//----------------SERVICES-----------
bool DeepReinforcedLandingUAV::getStatus(
    deep_reinforced_landing::GetDoneAndReward::Request &req,
    deep_reinforced_landing::GetDoneAndReward::Response &res) {
  res.done = done_;
  res.reward = reward_;
  res.wrong_altitude = wrong_altitude_;
  res.x = quadrotorPose_.position.x;
  res.y = quadrotorPose_.position.y;
  res.z = quadrotorPose_.position.z;
  res.orientation_x = quadrotorPose_.orientation.x;
  res.orientation_y = quadrotorPose_.orientation.y;
  res.orientation_z = quadrotorPose_.orientation.z;
  res.orientation_w = quadrotorPose_.orientation.w;
  return true;
}

bool DeepReinforcedLandingUAV::getCameraImage(
    deep_reinforced_landing::GetCameraImage::Request &req,
    deep_reinforced_landing::GetCameraImage::Response &res) {
  res.image = image_total_;
  return true;
}

bool DeepReinforcedLandingUAV::getNewCamera(
    deep_reinforced_landing::NewCameraService::Request &req,
    deep_reinforced_landing::NewCameraService::Response &res) {

  int size = out_.rows * out_.cols;
  for (int i = 0; i < size; i++) {
    res.image[i] = out_.at<int>(i);
  }
  return true;
}

bool DeepReinforcedLandingUAV::setModelState(
    deep_reinforced_landing::ResetPosition::Request &req,
    deep_reinforced_landing::ResetPosition::Response &res) {
  reset_ = req.reset;
  return true;
}

bool DeepReinforcedLandingUAV::sendCommand(
    deep_reinforced_landing::SendCommand::Request &req,
    deep_reinforced_landing::SendCommand::Response &res) {
  setActionCommand(req.command);

  float velocity = 0.5;
  if (req.command == "left") {
    velocity_cmd_.linear.y = velocity;
    can_move_ = true;
  } else if (req.command == "left_forward") {
    velocity_cmd_.linear.y = velocity;
    velocity_cmd_.linear.x = velocity;
    can_move_ = true;
  } else if (req.command == "right") {
    velocity_cmd_.linear.y = -velocity;
    can_move_ = true;
  } else if (req.command == "right_forward") {
    velocity_cmd_.linear.y = -velocity;
    velocity_cmd_.linear.x = velocity;
    can_move_ = true;
  } else if (req.command == "forward") {
    velocity_cmd_.linear.x = velocity;
    can_move_ = true;
  } else if (req.command == "backward") {
    velocity_cmd_.linear.x = -velocity;
    can_move_ = true;
  } else if (req.command == "left_backward") {
    velocity_cmd_.linear.y = velocity;
    velocity_cmd_.linear.x = -velocity;
    can_move_ = true;
  } else if (req.command == "right_backward") {
    velocity_cmd_.linear.y = -velocity;
    velocity_cmd_.linear.x = -velocity;
    can_move_ = true;
  } else if (req.command == "ascend") {
    velocity_cmd_.linear.z = velocity;
    can_move_ = true;
  } else if (req.command == "descend") {
    velocity_cmd_.linear.z = -0.2;
    can_move_ = true;
  } else if (req.command == "rotate_left") {
    velocity_cmd_.angular.z = velocity;
    can_move_ = true;
  } else if (req.command == "rotate_right") {
    velocity_cmd_.angular.z = -velocity;
    can_move_ = true;
  } else if (req.command == "takeoff") {
    can_takeoff_ = true;
  } else if (req.command == "land") {
    can_land_ = true;
  } else {
    velocity_cmd_.linear.x = velocity_cmd_.linear.y = velocity_cmd_.linear.z =
        0;
    velocity_cmd_.angular.x = velocity_cmd_.angular.y =
        velocity_cmd_.angular.z = 0;
    can_move_ = true;
  }

  return true;
}

bool DeepReinforcedLandingUAV::getRelativePose(
    deep_reinforced_landing::GetRelativePose::Request &req,
    deep_reinforced_landing::GetRelativePose::Response &res) {
  // NOTE: the relative position is calculated within the mathod for the reward
  // (setReward). Therefore that method need to be
  // called before this one in order to have the relative pose of the quadrotor
  // to the marker
  res.pose.position.x = quadrotor_to_marker_pose_.position.x;
  res.pose.position.y = quadrotor_to_marker_pose_.position.y;
  res.pose.position.z = quadrotor_to_marker_pose_.position.z;

  return true;
}
//----------------------------------

//-------CALLBACKS------------------

void DeepReinforcedLandingUAV::setDoneCallback(
    const ardrone_autonomy::Navdata &msg) {
  // if UAV landed, set done = true
  if (msg.state == LANDED_STATUS) {
    done_ = true;
  } else {
    done_ = false;
  }
}

void DeepReinforcedLandingUAV::getImageCallback(const sensor_msgs::Image &msg) {

  // Get color image
  image_total_ = msg;

  // Get greyscale
  src_ = cv_bridge::toCvCopy(msg, sensor_msgs::image_encodings::MONO8)->image;
  // Scale image to 84px x 150px
  cv::resize(src_, out_, cv::Size(), 0.233333333, 0.233333333);
  // cout << out_.cols << " " << out_.rows << " " << out_.size << endl;
  // Setup a rectangle to define a region of interest
  cv::Rect myROI(33, 0, 84, 84);
  // Crop the full image to that image contained by the rectangle myROI
  cv::Mat croppedRef(out_, myROI);

  // Copy the data in the original output image
  croppedRef.copyTo(out_);

  greyscale_camera_pub_.publish(
      (cv_bridge::CvImage(msg.header, "mono8", out_).toImageMsg()));
}

//---------------------------------

bool DeepReinforcedLandingUAV::getReset() { return reset_; }

void DeepReinforcedLandingUAV::setReset(bool reset) { reset_ = reset; }

gazebo_msgs::SetModelState DeepReinforcedLandingUAV::getModelState() {
  gazebo_msgs::SetModelState set_model_state = set_model_state_;
  float tmp_x, tmp_y, tmp_z;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::normal_distribution<> gaussian_xy(0, 0.75);

  tmp_x = markerPose_.position.x - bb_flight_half_size_ +
          (2 * bb_flight_half_size_ * rand()) /
              (RAND_MAX + 1.0); // spawn also inside the landing BB
  set_model_state.request.model_state.pose.position.x = tmp_x;

  tmp_y = markerPose_.position.y - bb_flight_half_size_ +
          (2 * bb_flight_half_size_ * rand()) /
              (RAND_MAX + 1.0); // spawn also inside the landing BB
  set_model_state.request.model_state.pose.position.y = tmp_y;

  std::random_device rand_dev;
  std::mt19937 generator(rand_dev());
  std::uniform_int_distribution<int> uniform_int(
      markerPose_.position.z + bb_landing_height_ + 1,
      markerPose_.position.z + 15.0 - bb_landing_height_);

  tmp_z = uniform_int(generator) / 1.0;
  set_model_state.request.model_state.pose.position.z = tmp_z;
  // std::cout << "[INFO]Altitute at the reset: " << tmp_z << std::endl;

  tfScalar roll, pitch, yaw;
  yaw = (rand() % 360); // - 180; // rand() supports only integer
  // cout << yaw << endl;
  tf::Matrix3x3 m;
  m.setEulerYPR(yaw, pitch, roll);
  tf::Quaternion orientation;
  m.getRotation(orientation);

  set_model_state.request.model_state.pose.orientation.x = orientation.getX();
  set_model_state.request.model_state.pose.orientation.y = orientation.getY();
  set_model_state.request.model_state.pose.orientation.z = orientation.getZ();
  set_model_state.request.model_state.pose.orientation.w = orientation.getW();

  return set_model_state;
}

void DeepReinforcedLandingUAV::setModelState(
    gazebo_msgs::SetModelState set_model_state) {
  set_state_client_.call(set_model_state);
}

bool DeepReinforcedLandingUAV::getCanMove() { return can_move_; }

bool DeepReinforcedLandingUAV::getCanLand() { return can_land_; }

bool DeepReinforcedLandingUAV::getCanTakeOff() { return can_takeoff_; }

ros::Publisher DeepReinforcedLandingUAV::getLandPub() { return land_pub_; }

ros::Publisher DeepReinforcedLandingUAV::getTakeoffPub() { return takeoff_pub_; }

ros::Publisher DeepReinforcedLandingUAV::getCmdPub() { return cmd_pub_; }

geometry_msgs::Twist DeepReinforcedLandingUAV::getVelocityCmd() {
  return velocity_cmd_;
}

void DeepReinforcedLandingUAV::setCanLand(bool can_land) { can_land_ = can_land; }

void DeepReinforcedLandingUAV::setCanTakeOff(bool can_takeoff) {
  can_takeoff_ = can_takeoff;
}

void DeepReinforcedLandingUAV::setCanMove(bool can_move) { can_move_ = can_move; }

int DeepReinforcedLandingUAV::getReward() { return reward_; }

void DeepReinforcedLandingUAV::setReward(double reward) { reward_ = reward; }

void DeepReinforcedLandingUAV::setReward() {

  srv_.request.model_name = "quadrotor";
  if (get_state_client_.call(srv_)) { // NB: quadrotor's altitude can be used to
                                      // understand if it still flying or landed
    quadrotorPose_.position.x = srv_.response.pose.position.x;
    quadrotorPose_.position.y = srv_.response.pose.position.y;
    quadrotorPose_.position.z = srv_.response.pose.position.z;
  } else {
    ROS_ERROR("Service has not been called");
  }

  srv_.request.model_name = "marker2";
  if (get_state_client_.call(srv_)) {
    markerPose_.position.x = srv_.response.pose.position.x;
    markerPose_.position.y = srv_.response.pose.position.y;
    markerPose_.position.z = srv_.response.pose.position.z;
    // Create a bounding box for autonomous landing given the marker's position
    // and a number
    bb_landing_.setDimension(markerPose_, bb_landing_half_size_,
                             bb_landing_height_);
    bb_flight_.setDimension(markerPose_, bb_flight_half_size_,
                            bb_flight_height_);
  } else {
    ROS_ERROR("Service has not been called");
  }

  // Calculate the quadrotor pose wrt the marker's one
  quadrotor_to_marker_pose_.position.x =
      quadrotorPose_.position.x - markerPose_.position.x;
  quadrotor_to_marker_pose_.position.y =
      quadrotorPose_.position.y - markerPose_.position.y;
  quadrotor_to_marker_pose_.position.z =
      quadrotorPose_.position.z - markerPose_.position.z;

  // setReward(utilities_.assignReward(quadrotorPose_, bb_landing_, bb_flight_,
  // &done_));

  setReward(utilities_.assignRewardWithoutFlightBB(
      quadrotorPose_, bb_landing_, bb_flight_, &done_, action_,
      &wrong_altitude_)); // for simulation_1/6
  // setReward(utilities_.assignRewardWhenLanding(quadrotorPose_, bb_landing_,
  // bb_flight_, &done_, action_)); // for simulation_2
}

void DeepReinforcedLandingUAV::setActionCommand(std::string action) {
  action_ = action;
  // cout << "Message received: " << action_ << endl;
}

int main(int argc, char **argv) {
  ros::init(argc, argv, "drl_services_node");
  DeepReinforcedLandingUAV drl_node;
  gazebo_msgs::SetModelState tmp_model_state;
  ros::Rate rate(30);
  std_msgs::Empty land_takeoff_cmd;
  double reward;

  while (ros::ok()) {

    // Calculate the reward at every iteration
    drl_node.setReward();

    // Reset position only if the reset service has been called;
    if (drl_node.getReset() == true) {
      tmp_model_state = drl_node.getModelState();
      drl_node.setModelState(tmp_model_state);
    }
    // then set to false the bool variable to not publish further msg at the
    // following iteration
    drl_node.setReset(false);

    // Send command if requested---------------
    if (drl_node.getCanTakeOff()) {
      drl_node.getTakeoffPub().publish(land_takeoff_cmd);
      drl_node.setCanTakeOff(false);
    } else if (drl_node.getCanLand()) {
      drl_node.getLandPub().publish(land_takeoff_cmd);
      drl_node.setCanLand(false);
    } else if (drl_node.getCanMove()) {
      drl_node.getCmdPub().publish(drl_node.getVelocityCmd());
      drl_node.setCanMove(false);
    }
    //-----------------------------------------

    ros::spinOnce();
    rate.sleep();
  }

  return 0;
}
