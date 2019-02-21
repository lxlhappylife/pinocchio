//
// Copyright (c) 2016-2018 CNRS, INRIA
//

#include "pinocchio/multibody/model.hpp"
#include "pinocchio/parsers/sample-models.hpp"
#include "pinocchio/algorithm/joint-configuration.hpp"
#include "pinocchio/math/quaternion.hpp"

#include <boost/test/unit_test.hpp>
#include <boost/utility/binary.hpp>

using namespace pinocchio;

bool configurations_are_equals(const Eigen::VectorXd & conf1, const Eigen::VectorXd & conf2)
{
  long size = conf1.size();
  if ( ! conf1.segment<3>(0).isApprox(conf2.segment<3>(0)) )
    return false;
  if( ! quaternion::defineSameRotation(Eigen::Quaterniond(conf1.segment<4>(3)), Eigen::Quaterniond(conf2.segment<4>(3))))
    return false;
  if( ! quaternion::defineSameRotation(Eigen::Quaterniond(conf1.segment<4>(7)), Eigen::Quaterniond(conf2.segment<4>(7))))
    return false;
  if ( ! conf1.segment(11, size-11).isApprox(conf2.segment(11, size-11)) )
    return false;
  return true;
}

template<typename D>
void addJointAndBody(Model & model, const JointModelBase<D> & jmodel, const Model::JointIndex parent_id, const SE3 & joint_placement, const std::string & name, const Inertia & Y)
{
  Model::JointIndex idx;
  typedef typename D::TangentVector_t TV;
  typedef typename D::ConfigVector_t CV;
  
  idx = model.addJoint(parent_id,jmodel,joint_placement,
                       name + "_joint",
                       TV::Zero(),
                       1e3 * (TV::Random() + TV::Constant(1)),
                       1e3 * (CV::Random() - CV::Constant(1)),
                       1e3 * (CV::Random() + CV::Constant(1))
                       );
  
  model.appendBodyToJoint(idx,Y,SE3::Identity());
}

void buildModel(Model & model)
{
  addJointAndBody(model,JointModelFreeFlyer(),model.getJointId("universe"),SE3::Identity(),"freeflyer",Inertia::Random());
  addJointAndBody(model,JointModelSpherical(),model.getJointId("freeflyer_joint"),SE3::Identity(),"spherical",Inertia::Random());
  addJointAndBody(model,JointModelPlanar(),model.getJointId("spherical_joint"),SE3::Identity(),"planar",Inertia::Random());
  addJointAndBody(model,JointModelRX(),model.getJointId("planar_joint"),SE3::Identity(),"rx",Inertia::Random());
  addJointAndBody(model,JointModelPX(),model.getJointId("rx_joint"),SE3::Identity(),"px",Inertia::Random());
  addJointAndBody(model,JointModelPrismaticUnaligned(SE3::Vector3(1,0,0)),model.getJointId("px_joint"),SE3::Identity(),"pu",Inertia::Random());
  addJointAndBody(model,JointModelRevoluteUnaligned(SE3::Vector3(0,0,1)),model.getJointId("pu_joint"),SE3::Identity(),"ru",Inertia::Random());
  addJointAndBody(model,JointModelSphericalZYX(),model.getJointId("ru_joint"),SE3::Identity(),"sphericalZYX",Inertia::Random());
  addJointAndBody(model,JointModelTranslation(),model.getJointId("sphericalZYX_joint"),SE3::Identity(),"translation",Inertia::Random());
}

BOOST_AUTO_TEST_SUITE ( BOOST_TEST_MODULE )

BOOST_AUTO_TEST_CASE ( integration_test )
{
  Model model; buildModel(model);

  std::vector<Eigen::VectorXd> qs(2);
  std::vector<Eigen::VectorXd> qdots(2);
  std::vector<Eigen::VectorXd> results(2);

  //
  // Test Case 0 : Integration of a config with zero velocity
  //
  qs[0] = Eigen::VectorXd::Ones(model.nq);
  qs[0].segment<4>(3) /= qs[0].segment<4>(3).norm(); // quaternion of freeflyer
  qs[0].segment<4>(7) /= qs[0].segment<4>(7).norm(); // quaternion of spherical joint
  qs[0].segment<2>(11+2) /= qs[0].segment<2>(11+2).norm(); // planar joint
 
  qdots[0] = Eigen::VectorXd::Zero(model.nv);
  results[0] = integrate(model,qs[0],qdots[0]);

  BOOST_CHECK_MESSAGE(results[0].isApprox(qs[0], 1e-12), "integration of full body with zero velocity - wrong results");
}

BOOST_AUTO_TEST_CASE ( interpolate_test )
{
  Model model; buildModel(model);

  Eigen::VectorXd q0(randomConfiguration(model, -1 * Eigen::VectorXd::Ones(model.nq), Eigen::VectorXd::Ones(model.nq) ));
  Eigen::VectorXd q1(randomConfiguration(model, -1 * Eigen::VectorXd::Ones(model.nq), Eigen::VectorXd::Ones(model.nq) ));

  Eigen::VectorXd q01_0 = interpolate(model,q0,q1,0.0);
  BOOST_CHECK_MESSAGE(isSameConfiguration(model,q01_0,q0), "interpolation: q01_0 != q0");

  Eigen::VectorXd q01_1 = interpolate(model,q0,q1,1.0);
  BOOST_CHECK_MESSAGE(isSameConfiguration(model,q01_1,q1), "interpolation: q01_1 != q1");
}

BOOST_AUTO_TEST_CASE ( diff_integration_test )
{
  Model model; buildModel(model);
  
  std::vector<Eigen::VectorXd> qs(2);
  std::vector<Eigen::VectorXd> vs(2);
  std::vector<Eigen::MatrixXd> results(2,Eigen::MatrixXd::Zero(model.nv,model.nv));
  std::vector<Eigen::MatrixXd> results_fd(2,Eigen::MatrixXd::Zero(model.nv,model.nv));
  
  qs[0] = Eigen::VectorXd::Ones(model.nq);
  qs[0].segment<4>(3) /= qs[0].segment<4>(3).norm(); // quaternion of freeflyer
  qs[0].segment<4>(7) /= qs[0].segment<4>(7).norm(); // quaternion of spherical joint
  qs[0].segment<2>(11+2) /= qs[0].segment<2>(11+2).norm(); // planar joint
  
  vs[0] = Eigen::VectorXd::Zero(model.nv);
  vs[1] = Eigen::VectorXd::Ones(model.nv);
  dIntegrate(model,qs[0],vs[0],results[0],ARG0);
  
  Eigen::VectorXd q_fd(model.nq), v_fd(model.nv); v_fd.setZero();
  const double eps = 1e-8;
  for(Eigen::DenseIndex k = 0; k < model.nv; ++k)
  {
    v_fd[k] = eps;
    q_fd = integrate(model,qs[0],v_fd);
    results_fd[0].col(k) = difference(model,qs[0],q_fd)/eps;
    v_fd[k] = 0.;
  }
  BOOST_CHECK(results[0].isIdentity(sqrt(eps)));
  BOOST_CHECK(results[0].isApprox(results_fd[0],sqrt(eps)));
  
  dIntegrate(model,qs[0],vs[0],results[1],ARG1);
  BOOST_CHECK(results[1].isApprox(results[0]));
  
  dIntegrate(model,qs[0],vs[1],results[0],ARG0);
  Eigen::VectorXd q_fd_intermediate(model.nq);
  Eigen::VectorXd q0_plus_v = integrate(model,qs[0],vs[1]);
  for(Eigen::DenseIndex k = 0; k < model.nv; ++k)
  {
    v_fd[k] = eps;
    q_fd_intermediate = integrate(model,qs[0],v_fd);
    q_fd = integrate(model,q_fd_intermediate,vs[1]);
    results_fd[0].col(k) = difference(model,q0_plus_v,q_fd)/eps;
    v_fd[k] = 0.;
  }
  
  BOOST_CHECK(results[0].isApprox(results_fd[0],sqrt(eps)));
  
  dIntegrate(model,qs[0],vs[1],results[1],ARG1);
  v_fd = vs[1];
  for(Eigen::DenseIndex k = 0; k < model.nv; ++k)
  {
    v_fd[k] += eps;
    q_fd = integrate(model,qs[0],v_fd);
    results_fd[1].col(k) = difference(model,q0_plus_v,q_fd)/eps;
    v_fd[k] -= eps;
  }
  
  BOOST_CHECK(results[1].isApprox(results_fd[1],sqrt(eps)));
}

BOOST_AUTO_TEST_CASE ( integrate_difference_test )
{
 Model model; buildModel(model);

 Eigen::VectorXd q0(randomConfiguration(model, -1 * Eigen::VectorXd::Ones(model.nq), Eigen::VectorXd::Ones(model.nq) ));
 Eigen::VectorXd q1(randomConfiguration(model, -1 * Eigen::VectorXd::Ones(model.nq), Eigen::VectorXd::Ones(model.nq) ));
 Eigen::VectorXd qdot(Eigen::VectorXd::Random(model.nv));

 BOOST_CHECK_MESSAGE(isSameConfiguration(model, integrate(model, q0, difference(model, q0,q1)), q1), "Integrate (difference) - wrong results");

 BOOST_CHECK_MESSAGE(difference(model, q0, integrate(model,q0, qdot)).isApprox(qdot),"difference (integrate) - wrong results");
}


BOOST_AUTO_TEST_CASE ( neutral_configuration_test )
{
  Model model; buildModel(model);

  Eigen::VectorXd expected(model.nq);
  expected << 0,0,0,0,0,0,1,
              0,0,0,1,
              0,0,1,0,
              0,
              0,
              0,
              0,
              0,0,0,
              0,0,0;


  Eigen::VectorXd neutral_config = neutral(model);
  BOOST_CHECK_MESSAGE(neutral_config.isApprox(expected, 1e-12), "neutral configuration - wrong results");
}

BOOST_AUTO_TEST_CASE ( distance_configuration_test )
{
  Model model; buildModel(model);
  
  Model::ConfigVectorType q0 = neutral(model);
  Model::ConfigVectorType q1(integrate (model, q0, Model::TangentVectorType::Ones(model.nv)));

  double dist = distance(model,q0,q1);
  
  BOOST_CHECK_MESSAGE(dist > 0., "distance - wrong results");
  BOOST_CHECK_SMALL(dist-difference(model,q0,q1).norm(), 1e-12);
}

BOOST_AUTO_TEST_CASE ( squared_distance_test )
{
  Model model; buildModel(model);

  Eigen::VectorXd q0(randomConfiguration(model, -1 * Eigen::VectorXd::Ones(model.nq), Eigen::VectorXd::Ones(model.nq) ));
  Eigen::VectorXd q1(randomConfiguration(model, -1 * Eigen::VectorXd::Ones(model.nq), Eigen::VectorXd::Ones(model.nq) ));

  double dist = distance(model,q0,q1);
  Eigen::VectorXd squaredDistance_ = squaredDistance(model,q0,q1);

  BOOST_CHECK_SMALL(dist-math::sqrt(squaredDistance_.sum()), 1e-12);
}

BOOST_AUTO_TEST_CASE ( uniform_sampling_test )
{
  Model model; buildModel(model);

  model.lowerPositionLimit = -1 * Eigen::VectorXd::Ones(model.nq);
  model.upperPositionLimit = Eigen::VectorXd::Ones(model.nq);
  Eigen::VectorXd q1(randomConfiguration(model));
  
  for (int i = 0; i < model.nq; ++i)
  {
    BOOST_CHECK_MESSAGE(q1[i] >= model.lowerPositionLimit[i] && q1[i] <= model.upperPositionLimit[i], " UniformlySample : Generated config not in bounds");
  }
}

BOOST_AUTO_TEST_CASE ( normalize_test )
{
  Model model; buildModel(model);

  Eigen::VectorXd q (Eigen::VectorXd::Ones(model.nq));
  pinocchio::normalize(model, q);

  BOOST_CHECK(q.head<3>().isApprox(Eigen::VectorXd::Ones(3)));
  BOOST_CHECK(fabs(q.segment<4>(3).norm() - 1) < Eigen::NumTraits<double>::epsilon()); // quaternion of freeflyer
  BOOST_CHECK(fabs(q.segment<4>(7).norm() - 1) < Eigen::NumTraits<double>::epsilon()); // quaternion of spherical joint
  const int n = model.nq - 7 - 4 - 4; // free flyer + spherical + planar
  BOOST_CHECK(q.tail(n).isApprox(Eigen::VectorXd::Ones(n)));
}

BOOST_AUTO_TEST_CASE ( integrateCoeffWiseJacobian_test )
{
  Model model; buildModels::humanoidRandom(model);
  
  Eigen::VectorXd q(Eigen::VectorXd::Ones(model.nq));
  pinocchio::normalize(model, q);
  
  Eigen::MatrixXd jac(model.nq,model.nv); jac.setZero();
 
  integrateCoeffWiseJacobian(model,q,jac);
  
 
  Eigen::MatrixXd jac_fd(model.nq,model.nv);
  Eigen::VectorXd q_plus;
  const double eps = 1e-8;
  
  Eigen::VectorXd v_eps(Eigen::VectorXd::Zero(model.nv));
  for(int k = 0; k < model.nv; ++k)
  {
    v_eps[k] = eps;
    q_plus = integrate(model,q,v_eps);
    jac_fd.col(k) = (q_plus - q)/eps;
    
    v_eps[k] = 0.;
  }
  BOOST_CHECK(jac.isApprox(jac_fd,sqrt(eps)));
}

BOOST_AUTO_TEST_SUITE_END ()
