/*
 *  navigation_handler.hpp
 *  LICENSE : BSD - https://raw.github.com/yujinrobot/yujin_ocs/license/LICENSE
 */


#include "yocs_navigator/semantic_navigator.hpp"

namespace yocs_navigator
{

void SemanticNavigator::processNavigation(yocs_msgs::NavigateToGoal::ConstPtr goal)
{
  std::string target_name = goal->location;
  int approach_type = goal->approach_type;
  unsigned int num_retry = goal->num_retry;
  double distance = goal->distance;
  double timeout = goal->timeout;

  bool target_found = findTarget(target_name,
                                 wps_,
                                 trajs_,
                                 target_wps_,
                                 target_wps_it_);

  if(!target_found) // if it fails to find the requested table
  {
    std::stringstream ss;
    ss << "Failed to find the requested destination : " << target_found;
    terminateNavigation(false, ss.str());
    return;
  }

  // TODO: Is this requirement really needed? Marcus
//  if(mtk::sameFrame(table.pose.header.frame_id, global_frame_) == false)
//  {
//    terminateNavigation(false, "Target is not in global frame");
//    return;
//  }
  clearCostmaps(); // TODO: probably remove this, since this should be handle by move_base

  while (true)
  {
    if (target_wps_it_ < target_wps_.end())
    {
      switch(approach_type)
      {
        case yocs_msgs::NavigateToGoal::APPROACH_NEAR:
          loginfo("Approach Type : NEAR");
          goNear(*target_wps_it_, distance, num_retry, timeout);
          break;
        case yocs_msgs::NavigateToGoal::APPROACH_ON:
          loginfo("Approach Type : ON");
          goOn(*target_wps_it_, distance, num_retry, timeout);
          break;
        default:
          terminateNavigation(false, "Invalid Approach Type");
          break;
      }
      target_wps_it_++;
    }
    else
    {
      loginfo("Reached final destination.");
    }
  }
}

void SemanticNavigator::goOn(const geometry_msgs::PoseStamped& target,
                             const double& in_distance,
                             const int& num_retry,
                             const double& timeout)
{
  int attempt = 0;
  int move_base_result;
  int navi_result;
  bool retry;
  bool final_result;
  std::string message;
  move_base_msgs::MoveBaseGoal mb_goal;
  
  ros::Time started_time = ros::Time::now();

  while(ros::ok())
  {
    move_base_result = NAVI_UNKNOWN;
    mb_goal.target_pose = target;
    ac_move_base_->sendGoal(mb_goal,
                            actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction>::SimpleDoneCallback(),
                            actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction>::SimpleActiveCallback(),
                            boost::bind(&SemanticNavigator::processMoveBaseFeedback, this, _1, target));
    
    waitForMoveBase(move_base_result, started_time, timeout);
    determineNavigationState(navi_result, move_base_result, ac_move_base_->getState());
    nextState(retry, final_result, message, navi_result, started_time);

    if(retry == false) break;
    else if(attempt > num_retry)
    {
      final_result = false;
      message = "Tried enough... I failed to navigate..";
      break;
    }

    attempt++;
    std::stringstream ss;
    ss << "Reattempt to navigate.. " << attempt;

    ros::Time current_time = ros::Time::now();
    double diff = (current_time - started_time).toSec();
    double remain_time = timeout - diff; 

    feedbackNavigation(yocs_msgs::NavigateToFeedback::STATUS_RETRY, distance_to_goal_, remain_time, ss.str()); 
    clearCostmaps();
  }

  terminateNavigation(final_result, message);
}

void SemanticNavigator::goNear(const geometry_msgs::PoseStamped& target,
                               const double& distance,
                               const int& num_retry,
                               const double& timeout)
{
  int attempt = 0;
  int move_base_result;
  int navi_result;
  bool retry;
  bool final_result;
  std::string message;
  move_base_msgs::MoveBaseGoal mb_goal;

  ros::Time started_time = ros::Time::now();
  
  while(attempt < num_retry && ros::ok())
  {
    move_base_result = NAVI_UNKNOWN;
    mb_goal.target_pose = target;
    ac_move_base_->sendGoal(mb_goal,
                            actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction>::SimpleDoneCallback(),
                            actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction>::SimpleActiveCallback(),
                            boost::bind(&SemanticNavigator::processMoveBaseFeedback, this, _1, target));
    waitForMoveBase(move_base_result, started_time, timeout);
    determineNavigationState(navi_result, move_base_result, ac_move_base_->getState());
    nextState(retry, final_result, message, navi_result, started_time);

    if(retry == false) break;
    
    attempt++;

    if(attempt > num_retry)
    {
      final_result = false;
      message = "Tried enough... I failed to navigate..";
      break;
    }
  }

  terminateNavigation(final_result, message);
}


void SemanticNavigator::processMoveBaseFeedback(const move_base_msgs::MoveBaseFeedback::ConstPtr& feedback,
                                                const geometry_msgs::PoseStamped& target_pose)
{
  geometry_msgs::PoseStamped robot_pose = feedback->base_position;

  // compute distance
  double distance = mtk::distance2D(robot_pose.pose.position.x,
                                    robot_pose.pose.position.y,
                                    target_pose.pose.position.x,
                                    target_pose.pose.position.y);
  distance_to_goal_ = distance;
}

} // namespace
