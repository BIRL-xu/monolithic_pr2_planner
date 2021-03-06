#pragma once
#include <monolithic_pr2_planner/StateReps/GraphState.h>
#include <monolithic_pr2_planner/StateReps/GoalState.h>
#include <memory>
#include <vector>
#include <boost/shared_ptr.hpp>

namespace monolithic_pr2_planner {
    /*! \brief The Abstract Heuristic class that has to be inherited
     * for use in the environment.
     */
    class AbstractHeuristic{
        public:
            AbstractHeuristic();

            // The function that has to return the heuristic value at the queried graph state
            virtual int getGoalHeuristic(GraphStatePtr state) = 0;
            virtual void setGoal(GoalState& state) = 0;

            // For 3D heuristics that need the obstacles - this function has to be implemented for the heuristic to receive the call for obstacle grid update.
            virtual void update3DHeuristicMap() {};

            // For 2D heuristics at the base that need only the map - this function has to be implemented for the heuristic to receive the call for map update.
            virtual void update2DHeuristicMap(const std::vector<signed char>& data) {};

            // Set the cost_multiplier
            void setCostMultiplier(const int cost_multiplier);

            // Get the cost multiplier
            int getCostMultiplier();
        private:
            int m_cost_multiplier;
    };
    typedef boost::shared_ptr<AbstractHeuristic> AbstractHeuristicPtr;
}
