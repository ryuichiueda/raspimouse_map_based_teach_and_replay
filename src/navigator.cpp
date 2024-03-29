#include <ros/ros.h>
#include <tf/transform_listener.h>
#include <ros/package.h> 
#include <iostream> 
#include <fstream> 
#include <iomanip> 
#include <vector> 
#include <string> 
#include <visualization_msgs/Marker.h>

#include "raspimouse_ros_2/ButtonValues.h"
#include "raspimouse_ros_2/LedValues.h"
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <geometry_msgs/Twist.h>

#include "visualization_msgs/MarkerArray.h"

using namespace ros;

bool load_finished = false;
std::vector<geometry_msgs::Point> trajectory;
visualization_msgs::MarkerArray marker_array;
int step = 0;

double x = 0.0;
double y = 0.0;
double yaw = 0.0;

bool replay_start = false;
bool replay_finished = false;

visualization_msgs::Marker markerBuilder(const geometry_msgs::Point &p, int id)
{
	visualization_msgs::Marker m;

	m.header.frame_id = "/map";
	m.type = 2;
	m.id = id;

	m.scale.x = 0.05;
	m.scale.y = 0.05;
	m.scale.z = 0.05;
	m.pose.position.x = p.x;
	m.pose.position.y = p.y;

	m.color.r = 0.0;
	m.color.g = 1.0;
	m.color.b = 0.0;
	m.color.a = 0.5;

	return m;
}

geometry_msgs::Twist decision(double distance, double direction)
{
	geometry_msgs::Twist tw;

	if(direction > 180.0) direction -= 360.0;
	if(direction < -180.0) direction += 360.0;

	if( (fabs(direction) > 10.0 and distance < 0.1) or fabs(direction) > 30.0 ){
		tw.linear.x /= 1.1;
	}else{
		if(tw.linear.x < 0.05)
			tw.linear.x = 0.05;
		tw.linear.x *= 2.0;
		if(tw.linear.x > 0.4)
			tw.linear.x = 0.4;
	}

	tw.angular.z = 2*direction*3.141592/180;
	if(tw.angular.z > 2.0)       tw.angular.z = 2.0;
	else if(tw.angular.z < -2.0) tw.angular.z = -2.0;

	return tw;
}

bool read_trajectory(std::string filename)
{
	std::ifstream f(filename);
	if(!f)
		return false;

	double x,y;
	while(f >> x >> y){
		geometry_msgs::Point p;
		p.x = x;
		p.y = y;
		p.z = 0.0;
		trajectory.push_back(p);
	}

	if(trajectory.size() == 0)
		return false;

	int n = 0;
	for(auto p : trajectory){
		ROS_INFO("point %d\t%f\t%f", n++, p.x, p.y);
	}

	return true;
}

void callbackPose(const geometry_msgs::PoseWithCovarianceStamped::ConstPtr& msg)
{
	x = msg->pose.pose.position.x; 
	y = msg->pose.pose.position.y; 
	yaw = tf::getYaw(msg->pose.pose.orientation)/3.141592*180;
}

void callbackButtons(const raspimouse_ros_2::ButtonValues::ConstPtr& msg)
{
	if (msg->mid){
		ROS_INFO("replay start");
		replay_start = true;
	}
	if (msg->rear){
		ROS_INFO("replay finished");
		replay_finished = true;
	}
}

int main(int argc, char **argv)
{
	read_trajectory("/tmp/trajectory.tsv");
	int marker_id = 0;
	for(auto p : trajectory){
		marker_array.markers.push_back( markerBuilder(p, marker_id++) );
	}

	init(argc,argv,"navigator");
	NodeHandle n;

	Subscriber sub_pose = n.subscribe("/amcl_pose", 1, callbackPose);
	Subscriber sub_button = n.subscribe("/buttons", 1, callbackButtons);
	Publisher pub_cmd_vel = n.advertise<geometry_msgs::Twist>("/cmd_vel", 1);
	Publisher pub_led = n.advertise<raspimouse_ros_2::LedValues>("leds", 5);
	Publisher pub_trajectory = n.advertise<visualization_msgs::MarkerArray>("trajectory", 5);
	Publisher pub_target = n.advertise<visualization_msgs::Marker>("target_marker", 5);

	raspimouse_ros_2::LedValues leds;
	leds.left_side = false;
	leds.right_side = false;
	leds.left_forward = true;
	leds.right_forward = false;

	Rate r(10);
	while(ok()){ //waiting the center button
		pub_trajectory.publish(marker_array);

                pub_led.publish(leds);
		if(replay_start)
			break;
		spinOnce();
		r.sleep();
	}

	geometry_msgs::Twist tw;
	tw.linear.x = 0.0;
	tw.angular.z = 0.0;

	int step = 0;
	int prev_step = trajectory.size() - 1;

	marker_array.markers[step].color.r = 1.0;
	marker_array.markers[step].color.g = 0.0;
	pub_trajectory.publish(marker_array);

	while(ok()){
		double prev_x = trajectory[prev_step].x;
		double prev_y = trajectory[prev_step].y;
		double target_x = trajectory[step].x;
		double target_y = trajectory[step].y;

		double target_r = sqrt((target_x-x)*(target_x-x) + (target_y-y)*(target_y-y));

		if(target_r < 0.05){
			prev_step = step;
			step = (step+1)%trajectory.size();

			marker_array.markers[step].color.r = 1.0;
			marker_array.markers[step].color.g = 0.0;
			marker_array.markers[prev_step].color.r = 0.0;
			marker_array.markers[prev_step].color.g = 1.0;
			pub_trajectory.publish(marker_array);

			continue;
		}

		double target_direction = atan2(target_y-y, target_x-x)/3.141592*180 - yaw;

		geometry_msgs::Twist tw = decision(target_r, target_direction);
		pub_cmd_vel.publish(tw);

		if(replay_finished)
			break;

		spinOnce();
		r.sleep();
	}

	return 0;
}
