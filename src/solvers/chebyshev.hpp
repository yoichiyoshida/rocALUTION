#ifndef ROCALUTION_KRYLOV_CHEBYSHEV_HPP_
#define ROCALUTION_KRYLOV_CHEBYSHEV_HPP_

#include "solver.hpp"

#include <vector>

namespace rocalution {

template <class OperatorType, class VectorType, typename ValueType>
class Chebyshev : public IterativeLinearSolver<OperatorType, VectorType, ValueType> {

public:

  Chebyshev();
  virtual ~Chebyshev();

  virtual void Print(void) const;

  void Set(const ValueType lambda_min, const ValueType lambda_max);

  virtual void Build(void);
  virtual void ReBuildNumeric(void);
  virtual void Clear(void);

protected:

  virtual void SolveNonPrecond_(const VectorType &rhs,
                                VectorType *x);
  virtual void SolvePrecond_(const VectorType &rhs,
                             VectorType *x);

  virtual void PrintStart_(void) const;
  virtual void PrintEnd_(void) const;

  virtual void MoveToHostLocalData_(void);
  virtual void MoveToAcceleratorLocalData_(void);

private:

  bool init_lambda_;
  ValueType lambda_min_, lambda_max_;

  VectorType r_, z_;
  VectorType p_;

};


}

#endif // ROCALUTION_KRYLOV_CHEBYSHEV_HPP_
