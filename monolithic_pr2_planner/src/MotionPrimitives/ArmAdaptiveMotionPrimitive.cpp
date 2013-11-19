#include <monolithic_pr2_planner/MotionPrimitives/ArmAdaptiveMotionPrimitive.h>
#include <monolithic_pr2_planner/StateReps/DiscObjectState.h>
#include <monolithic_pr2_planner/LoggerNames.h>
#include <monolithic_pr2_planner/Visualizer.h>
#include <cmath>

using namespace monolithic_pr2_planner;
using namespace std;

GoalState ArmAdaptiveMotionPrimitive::m_goal;

// The arm adaptive motion dynamically generates a motion that moves the current
// source_state directly to the goal state. This only runs if the Euclidean
// distance of the object state is sufficiently close to the goal - then all
// that's left to do is align the roll, pitch, and yaw to the goal state. This
// can fail in the yaw of the object is drastically different from the
// source_state's yaw (in that case, the base probably needs to be moved, which
// is done by the BaseAdaptiveMotionPrimitive). 
bool ArmAdaptiveMotionPrimitive::apply(const GraphState& source_state,
                                  GraphStatePtr& successor){
    DiscObjectState goal = m_goal.getObjectState();
    // TODO parameterize this distance?
    if (dist(source_state.getObjectStateRelMap(), goal) > 2){
        return false;
    }

    DiscBaseState base_state = source_state.robot_pose().base_state();
    


    // take the discrete orientation (theta) of the robot and convert it
    // into the discrete yaw in the object frame. object frame yaw is
    // generally discretized into 64 states, whereas the base theta is 16.
    ContBaseState c_base = source_state.robot_pose().base_state();
    ContObjectState c_obj_state = m_goal.getObjectState();
    c_obj_state.yaw(c_base.theta());

    // convert the object's rpy from map frame to body frame. Since the robot is
    // always on a flat surface, the roll and pitch of the map and body frame
    // are aligned - the only thing that needs to be transformed is the yaw.
    DiscObjectState obj_in_body_frame = source_state.getObjectStateRelBody();
    obj_in_body_frame.roll(goal.roll());
    obj_in_body_frame.pitch(goal.pitch());

    // this line is needed because we need to convert from disc
    ContObjectState c_goal(goal);
    ROS_DEBUG_NAMED(MPRIM_LOG, "c obj state %f goal yaw %f", c_obj_state.yaw(), c_goal.yaw());
    double short_ang = shortest_angular_distance(c_obj_state.yaw(),c_goal.yaw());
    ContObjectState blah(obj_in_body_frame);
    blah.yaw(short_ang);
    DiscObjectState blah2 = blah;
    ROS_DEBUG_NAMED(MPRIM_LOG, "obj_in_body_yaw %f", blah.yaw());
    obj_in_body_frame.yaw(blah2.yaw());

    RobotState seed_pose(base_state, 
                         source_state.robot_pose().right_arm(), 
                         source_state.robot_pose().left_arm());
    RobotPosePtr successor_robot_pose;
    bool isIKSuccess = RobotState::computeRobotPose(obj_in_body_frame, 
                                                    seed_pose, 
                                                    successor_robot_pose);
    if (isIKSuccess){
        ROS_DEBUG_NAMED(MPRIM_LOG, "successful arm adaptive motion!");
        ContObjectState new_obj_state = successor_robot_pose->getObjectStateRelMap();
        new_obj_state.printToDebug(MPRIM_LOG);
        //assert(fabs(new_obj_state.x() == c_obj_state.x()) < .0001);
        //assert(fabs(new_obj_state.y() == c_obj_state.y()) < .0001);
        //assert(fabs(new_obj_state.z()-c_obj_state.z()) < .0001);
        assert(fabs(new_obj_state.roll() == c_obj_state.roll()) < .0001);
        assert(fabs(new_obj_state.pitch()-c_obj_state.pitch()) < .0001);
        assert(fabs(new_obj_state.yaw()-c_obj_state.yaw()) < .0001);
        successor = boost::make_shared<GraphState>(*successor_robot_pose);
    } else {
        ROS_DEBUG_NAMED(MPRIM_LOG, "IK failed on arm AMP");
    }
    return isIKSuccess;
}


void ArmAdaptiveMotionPrimitive::print() const {
    ROS_DEBUG_NAMED(MPRIM_LOG, 
                    "ArmAdaptiveMotionPrimitive cost %d", cost());
}

void ArmAdaptiveMotionPrimitive::computeCost(const MotionPrimitiveParams& params){
    m_cost = 1;
}
