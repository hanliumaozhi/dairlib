#pragma once

#include "dircon_kinematic_constraint.h"

namespace drake{
class DirconOptions {
  private:
    int n_constraints_;
    std::vector<bool> is_constraints_relative_;
    bool constrain_start_;
    bool constrain_end_;
    bool constrain_phi_start_;
    bool constrain_phi_end_;
    double accel_cost_;  

  public:
    DirconOptions(int n_constraints);
    ~DirconOptions(void);

    void setAllConstraintsRelative(bool relative);
    void setConstraintRelative(int index, bool relative);
    void setConstraintStart(bool constrain_start);
    void setConstraintEnd(bool constrain_end);
    void setConstraintPhiStart(bool constrain_phi_start);
    void setConstraintPhiEnd(bool constrain_phi_end);
    void setAccelCost(double accel_cost);

    int getNumConstraints();
    bool getSingleConstraintRelative(int index);
    bool getConstrainStart();
    bool getConstrainEnd();
    bool getConstrainPhiStart();
    bool getConstrainPhiEnd();
    double getAccelCost();      
};
}