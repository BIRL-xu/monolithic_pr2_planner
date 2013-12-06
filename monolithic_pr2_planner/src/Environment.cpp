#include <monolithic_pr2_planner/Environment.h>
#include <monolithic_pr2_planner/OccupancyGridUser.h>
#include <monolithic_pr2_planner/StateReps/RobotState.h>
#include <monolithic_pr2_planner/StateReps/DiscBaseState.h>
#include <monolithic_pr2_planner/StateReps/ContArmState.h>
#include <monolithic_pr2_planner/MotionPrimitives/ArmAdaptiveMotionPrimitive.h>
#include <monolithic_pr2_planner/Visualizer.h>
#include <monolithic_pr2_planner/Constants.h>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <assert.h>

#define GOAL_STATE 1
using namespace monolithic_pr2_planner;
using namespace boost;

typedef scoped_ptr<SearchRequest> SearchRequestPtr;

// TODO yuck @ stateid2mapping pointer
Environment::Environment(ros::NodeHandle nh) : 
    m_hash_mgr(&StateID2IndexMapping), m_nodehandle(nh), m_mprims(m_goal){
    m_param_catalog.fetch(nh);
    configurePlanningDomain();
}

bool Environment::configureRequest(SearchRequestParamsPtr search_request_params,
                                   int& start_id, int& goal_id){
    SearchRequestPtr search_request = SearchRequestPtr(new SearchRequest(search_request_params));
    configureQuerySpecificParams(search_request);
    if (!setStartGoal(search_request, start_id, goal_id)){
        return false;
    }

    return true;
}

int Environment::GetGoalHeuristic(int stateID){
    return m_heur->getGoalHeuristic(m_hash_mgr.getGraphState(stateID));
}

void Environment::GetSuccs(int sourceStateID, vector<int>* succIDs, 
                           vector<int>* costs){
    assert(sourceStateID != GOAL_STATE);
    ROS_DEBUG_NAMED(SEARCH_LOG, 
            "==================Expanding state %d==================", 
                    sourceStateID);
    succIDs->clear();
    succIDs->reserve(m_mprims.getMotionPrims().size());
    costs->clear();
    costs->reserve(m_mprims.getMotionPrims().size());

    GraphStatePtr source_state = m_hash_mgr.getGraphState(sourceStateID);
    ROS_DEBUG_NAMED(SEARCH_LOG, "Source state is:");
    source_state->robot_pose().printToDebug(SEARCH_LOG);
    source_state->robot_pose().visualize();
    sleep(.5);

    if (GetGoalHeuristic(sourceStateID) < 5){
        ROS_INFO("close to the goal");
    }

    for (auto mprim : m_mprims.getMotionPrims()){
        ROS_DEBUG_NAMED(SEARCH_LOG, "Applying motion:");
        mprim->printEndCoord();
        GraphStatePtr successor;
        TransitionData t_data;
        if (!mprim->apply(*source_state, successor, t_data)){
            ROS_DEBUG_NAMED(MPRIM_LOG, "couldn't apply mprim");
            continue;
        }
        if (m_cspace_mgr->isValidSuccessor(*successor, t_data) && 
            m_cspace_mgr->isValidTransitionStates(t_data)){
            //ROS_DEBUG_NAMED(SEARCH_LOG, "Source state is:");
            //source_state->printToDebug(SEARCH_LOG);
            //ROS_DEBUG_NAMED(SEARCH_LOG, 
            //"==================Applying Motion Primitive==================");
            //ROS_DEBUG_NAMED(MPRIM_LOG, "Applying motion:");
            //mprim->printEndCoord();
            m_hash_mgr.save(successor);
            //ROS_DEBUG_NAMED(MPRIM_LOG, "successor state with id %d is:", 
            //                successor->id());
            //successor->printToDebug(MPRIM_LOG);


            if (m_goal->isSatisfiedBy(successor)){
                m_goal->storeAsSolnState(successor);
                ROS_INFO_NAMED(SEARCH_LOG, "Found potential goal at state %d", successor->id());
                succIDs->push_back(GOAL_STATE);
            } else {
                succIDs->push_back(successor->id());
            }
            //costs->push_back(1);
            costs->push_back(mprim->cost());
            ROS_DEBUG_NAMED(SEARCH_LOG, "motion succeeded with cost %d", mprim->cost());
        } else {
            successor->robot_pose().visualize();
            ROS_DEBUG_NAMED(SEARCH_LOG, "successor failed collision checking");
        }
    }
}

bool Environment::setStartGoal(SearchRequestPtr search_request,
                               int& start_id, int& goal_id){
    RobotState start_pose(search_request->m_params->base_start, 
                         search_request->m_params->right_arm_start,
                         search_request->m_params->left_arm_start);
    ContObjectState obj_state = start_pose.getObjectStateRelMap();
    obj_state.printToInfo(SEARCH_LOG);

    if (!search_request->isValid(m_cspace_mgr)){
        obj_state.printToInfo(SEARCH_LOG);
        start_pose.visualize();
        return false;
    }

    GraphStatePtr start_graph_state = make_shared<GraphState>(start_pose);
    m_hash_mgr.save(start_graph_state);
    start_id = start_graph_state->id();
    assert(m_hash_mgr.getGraphState(start_graph_state->id()) == start_graph_state);

    ROS_INFO_NAMED(SEARCH_LOG, "Start state set to:");
    start_pose.printToInfo(SEARCH_LOG);
    obj_state.printToInfo(SEARCH_LOG);
    start_pose.visualize();


    m_goal = make_shared<GoalState>(search_request, m_heur);

    // TODO fix this! shouldn't be saving the start state twice.
    GraphStatePtr fake_goal = make_shared<GraphState>(*start_graph_state);
    RobotState fake_robot_state = fake_goal->robot_pose();
    DiscBaseState fake_base = fake_robot_state.base_state();
    fake_base.x(0); fake_base.y(0); fake_base.z(0);
    fake_robot_state.base_state(fake_base);
    fake_goal->robot_pose(fake_robot_state);
    m_hash_mgr.save(fake_goal);
    goal_id = fake_goal->id();

    ROS_INFO_NAMED(SEARCH_LOG, "Goal state created:");
    ContObjectState c_goal = m_goal->getObjectState();
    c_goal.printToInfo(SEARCH_LOG);
    m_goal->visualize();

    // This informs the adaptive motions about the goal.
    ArmAdaptiveMotionPrimitive::goal(*m_goal);
    BaseAdaptiveMotionPrimitive::goal(*m_goal);

    return true;
}


// this sets up the environment for things that are query independent.
void Environment::configurePlanningDomain(){
    // used for collision space and discretizing plain xyz into grid world 
    OccupancyGridUser::init(m_param_catalog.m_occupancy_grid_params,
                            m_param_catalog.m_robot_resolution_params);
    // init empty heuristic
    m_heur = make_shared<Heuristic>();

    // used for discretization of robot movements
    ContArmState::setRobotResolutionParams(m_param_catalog.m_robot_resolution_params);

#ifdef USE_IKFAST_SOLVER
    ROS_INFO_NAMED(CONFIG_LOG, "Using IKFast");
#endif
#ifdef USE_KDL_SOLVER
    ROS_INFO_NAMED(CONFIG_LOG, "Using KDL");
#endif


    // used for arm kinematics
    LeftContArmState::initArmModel(m_param_catalog.m_left_arm_params);
    RightContArmState::initArmModel(m_param_catalog.m_right_arm_params);


    // collision space mgr needs arm models in order to do collision checking
    // have to do this funny thing  of initializing an object because of static
    // variable + inheritance (see ContArmState for details)
    LeftContArmState l_arm;
    RightContArmState r_arm;
    m_cspace_mgr = make_shared<CollisionSpaceMgr>(r_arm.getArmModel(),
                                                  l_arm.getArmModel(),
                                                  m_heur);
    

    // load up motion primitives
    m_mprims.loadMPrims(m_param_catalog.m_motion_primitive_params);

    // load up static pviz instance for visualizations. 
    Visualizer::createPVizInstance();
    Visualizer::setReferenceFrame(std::string("/map"));

}

// sets parameters for query specific things
void Environment::configureQuerySpecificParams(SearchRequestPtr search_request){
    // sets the location of the object in the frame of the wrist
    // have to do this funny thing  of initializing an object because of static
    // variable + inheritance (see ContArmState for details)
    LeftContArmState l_arm;
    RightContArmState r_arm;
    l_arm.setObjectOffset(search_request->m_params->left_arm_object);
    r_arm.setObjectOffset(search_request->m_params->right_arm_object);
    RobotState::setPlanningMode(search_request->m_params->planning_mode);
}

/*! \brief Given the solution path containing state IDs, reconstruct the
 * actual corresponding robot states. This also makes the path smooth in between
 * each state id because we add in the intermediate states given by the
 * transition data.
 */
vector<FullBodyState> Environment::reconstructPath(vector<int> soln_path){
    vector<TransitionData> transition_states;
    // the last state in the soln path return by the SBPL planner will always be
    // the goal state ID. Since this doesn't actually correspond to a real state
    // in the heap, we have to look it up.
    soln_path[soln_path.size()-1] = m_goal->getSolnState()->id();
    ROS_DEBUG_NAMED(SEARCH_LOG, "setting goal state id to %d", 
                                 m_goal->getSolnState()->id());
    for (size_t i=0; i < soln_path.size()-1; i++){
        TransitionData best_transition;
        bool success = findBestTransition(soln_path[i], 
                                          soln_path[i+1], 
                                          best_transition);
        if (success){
            transition_states.push_back(best_transition);
        } else {
            ROS_ERROR("Successor not found during path reconstruction!");
        }
    }
    
    vector<FullBodyState> final_path = getFinalPath(soln_path, 
                                                    transition_states);
    visualizeFinalPath(final_path);
    return final_path;
}

void Environment::visualizeFinalPath(vector<FullBodyState> path){
    for (auto& state : path){
        vector<double> l_arm, r_arm, base;
        l_arm = state.left_arm;
        r_arm = state.right_arm;
        base = state.base;
        BodyPose bp;
        bp.x = base[0];
        bp.y = base[1];
        bp.z = base[2];
        bp.theta = base[3];
        Visualizer::pviz->visualizeRobot(r_arm, l_arm, bp, 150, "robot", 0);
        usleep(10000);
    }
}

/*! \brief Given a start and end state id, find the motion primitive with the
 * least cost that gets us from start to end. Return that information with a
 * TransitionData object.
 */
bool Environment::findBestTransition(int start_id, int end_id, 
                                     TransitionData& best_transition){
    ROS_DEBUG_NAMED(SEARCH_LOG, "searching from %d for successor id %d", 
                                start_id, end_id);
    GraphStatePtr source_state = m_hash_mgr.getGraphState(start_id);
    GraphStatePtr successor;
    int best_cost = 1000000;
    GraphStatePtr real_next_successor = m_hash_mgr.getGraphState(end_id);
    for (auto mprim : m_mprims.getMotionPrims()){
        TransitionData t_data;
        if (!mprim->apply(*source_state, successor, t_data)){
            continue;
        }
        if (!m_cspace_mgr->isValidSuccessor(*successor, t_data) ||  
                !m_cspace_mgr->isValidTransitionStates(t_data)){
            continue;
        }
        successor->id(m_hash_mgr.getStateID(successor));
        if ((successor->id() == end_id)){
            if (t_data.cost() < best_cost){
                best_cost = t_data.cost();
                best_transition = t_data;
                best_transition.successor_id(successor->id());
            } 
        }
    }
    return (best_cost != 1000000);
}

FullBodyState Environment::createFBState(const RobotState& robot){
    vector<double> l_arm, r_arm, base;
    robot.right_arm().getAngles(&r_arm);
    robot.left_arm().getAngles(&l_arm);
    ContBaseState c_base = robot.base_state();
    c_base.getValues(&base);
    FullBodyState state;
    state.left_arm = l_arm;
    state.right_arm = r_arm;
    state.base = base;
    return state;
}

/*! \brief Retrieve the final path, with intermediate states and all. There's
 * some slight funny business with TransitionData for intermediate base states
 * because they're not captured in the RobotState. Instead, they're held
 * separately in TransitionData ONLY if the motion primitive has the base
 * moving. There's one more state_id than transition state.
 * */
std::vector<FullBodyState> Environment::getFinalPath(const vector<int>& state_ids,
                                 const vector<TransitionData>& transition_states){
    vector<FullBodyState> fb_states;
    { // throw in the first point
        GraphStatePtr source_state = m_hash_mgr.getGraphState(state_ids[0]);
        fb_states.push_back(createFBState(source_state->robot_pose()));
    }
    for (size_t i=0; i < transition_states.size(); i++){
        int motion_type = transition_states[i].motion_type();
        bool isInterpBaseMotion = (motion_type == MPrim_Types::BASE || 
                                   motion_type == MPrim_Types::BASE_ADAPTIVE);
        for (size_t j=0; j < transition_states[i].interm_robot_steps().size(); j++){
            RobotState robot = transition_states[i].interm_robot_steps()[j];
            FullBodyState state = createFBState(robot);
            if (isInterpBaseMotion){
                assert(transition_states[i].interm_robot_steps().size() == 
                       transition_states[i].cont_base_interm_steps().size());
                ContBaseState cont_base = transition_states[i].cont_base_interm_steps()[j];
                std::vector<double> base;
                cont_base.getValues(&base);
                state.base = base;
            } 
            fb_states.push_back(state);
        }
    }
    { // throw in the last point
        GraphStatePtr soln_state = m_goal->getSolnState();
        fb_states.push_back(createFBState(soln_state->robot_pose()));
    }
    return fb_states;
}

bool Environment::InitializeMDPCfg(MDPConfig *MDPCfg) {
  return true;
}

int Environment::SizeofCreatedEnv() {
    return m_hash_mgr.size();
}

void Environment::PrintState(int stateID, bool bVerbose, FILE* fOut /*=NULL*/) {

}
