/* ===========================
  Copyright (c) 2013 Philippe Tillet
  UMinTL - Unconstrained Minimization Template Library

  License : MIT X11 - See the LICENSE file in the root folder
 * ===========================*/


#ifndef UMINTL_MINIMIZE_HPP_
#define UMINTL_MINIMIZE_HPP_
#include <cmath>

#include "umintl/optimization_result.hpp"

#include "umintl/model_base.hpp"

#include "umintl/function_wrapper.hpp"
#include "umintl/optimization_context.hpp"

#include "umintl/directions/conjugate_gradient.hpp"
#include "umintl/directions/quasi_newton.hpp"
#include "umintl/directions/low_memory_quasi_newton.hpp"
#include "umintl/directions/steepest_descent.hpp"
#include "umintl/directions/truncated_newton.hpp"

#include "umintl/line_search/strong_wolfe_powell.hpp"

#include "umintl/stopping_criterion/value_treshold.hpp"
#include "umintl/stopping_criterion/gradient_treshold.hpp"

#include <iomanip>
#include <sstream>

namespace umintl{

    /** @brief The minimizer class
     *
     *  @tparam BackendType the linear algebra backend of the minimizer
     */
    template<class BackendType>
    class minimizer{

        class IosFlagSaver {
        public:
            explicit IosFlagSaver(std::ostream& _ios):
                ios(_ios),
                f(nullptr)
            {
                f.copyfmt(ios);
            }
            ~IosFlagSaver() {
                ios.copyfmt(f);
            }

            IosFlagSaver(const IosFlagSaver &rhs) = delete;
            IosFlagSaver& operator= (const IosFlagSaver& rhs) = delete;

        private:
            std::ostream& ios;
            std::ios f;
        };

    public:

        /** @brief The constructor
         *
         * @param _direction the descent direction used by the minimizer
         * @param _stopping_criterion the stopping criterion
         * @param _iter the maximum number of iterations
         * @param _verbose the verbose level
         */
        minimizer(umintl::direction<BackendType> * _direction = new quasi_newton<BackendType>()
                             , umintl::stopping_criterion<BackendType> * _stopping_criterion = new gradient_treshold<BackendType>()
                             , unsigned int _iter = 1024, unsigned int _verbose = 0) :
            direction(_direction)
          , line_search(new strong_wolfe_powell<BackendType>())
          , stopping_criterion(_stopping_criterion)
          , model(new deterministic<BackendType>())
          , hessian_vector_product_computation(CENTERED_DIFFERENCE)
          , verbose(_verbose), iter(_iter){

        }

        tools::shared_ptr<umintl::direction<BackendType> > direction;
        tools::shared_ptr<umintl::line_search<BackendType> > line_search;
        tools::shared_ptr<umintl::stopping_criterion<BackendType> > stopping_criterion;
        tools::shared_ptr< model_base<BackendType> > model;
        computation_type hessian_vector_product_computation;

        double tolerance;

        unsigned int verbose;
        unsigned int iter;

    private:

        /** @brief Get a brief info string on the minimizer
         *
         *  @return String containing the verbose level, maximum number of iteration, and the direction used
         */
        std::string info() const{
          std::ostringstream oss;
          oss << "Verbosity Level : " << verbose << std::endl;
          oss << "Maximum number of iterations : " << iter << std::endl;
          oss << "Direction : " << direction->info() << std::endl;
          return oss.str();
        }

        /** @brief Clean memory and terminate the optimization result
         *
         *  @return Optimization result
         */
        optimization_result terminate(optimization_result::termination_cause_type termination_cause, typename BackendType::VectorType & res, size_t N, optimization_context<BackendType> & context){
            optimization_result result;
            BackendType::copy(N,context.x(),res);
            result.f = context.val();
            result.iteration = context.iter();
            result.n_functions_eval = context.fun().n_value_computations();
            result.n_gradient_eval = context.fun().n_gradient_computations();
            result.termination_cause = termination_cause;

            clean_all(context);

            return result;
        }

        /** @brief Init the components of the procedure (ie allocate memory for the temporaries, typically)
         */
        void init_all(optimization_context<BackendType> & c){
            direction->init(c);
            line_search->init(c);
            stopping_criterion->init(c);
        }

        /** @brief Clean the components of the procedure (ie free memory for the temporaries, typically)
         */
        void clean_all(optimization_context<BackendType> & c){
            direction->clean(c);
            line_search->clean(c);
            stopping_criterion->clean(c);
        }

    public:
      template<class Fun, typename Trace_type>
      optimization_result operator()(typename BackendType::VectorType & res, Fun & fun, typename BackendType::VectorType const & x0, size_t N, Trace_type trace){

            tools::shared_ptr<umintl::direction<BackendType> > steepest_descent(new umintl::steepest_descent<BackendType>());
            line_search_result<BackendType> search_res(N);
            optimization_context<BackendType> c(x0, N, *model, new detail::function_wrapper_impl<BackendType, Fun>(fun,N,hessian_vector_product_computation));

            init_all(c);

            tools::shared_ptr<umintl::direction<BackendType> > current_direction;
            if(dynamic_cast<truncated_newton<BackendType> * >(direction.get()))
              current_direction = direction;
            else
              current_direction = steepest_descent;

            //Main loop
            c.fun().compute_value_gradient(c.x(), c.val(), c.g(), c.model().get_value_gradient_tag());
            for( ; c.iter() < iter ; ++c.iter()){
              trace(c.iter(), c.x(), std::sqrt(N), std::sqrt(N));
                if(verbose >= 1 ){
                    IosFlagSaver flags_saver(std::cout);
                    std::cout << "Iteration " << std::setw(4) << c.iter()
                              << ": cost=" << std::fixed << std::setw(6) << std::setprecision(4) << c.val()
                              << "; NV=" << std::setw(4) << c.fun().n_value_computations()
                              << "; NG=" << std::setw(4) << c.fun().n_gradient_computations();
                    if(dynamic_cast<truncated_newton<BackendType>*>(direction.get()))
                        std::cout<< "; NH=" << std::setw(4) << c.fun().n_hessian_vector_product_computations() ;
                    if(unsigned int ND = c.fun().n_datapoints_accessed())
                     std::cout << "; NPoints=" << std::scientific << std::setprecision(3) << (float)ND;
                    std::cout << std::endl;
                }

                (*current_direction)(c);

                c.dphi_0() = BackendType::dot(N,c.p(),c.g());
                //Not a descent direction...
                if(c.dphi_0()>=0){
                    //current_direction->reset(c);
                    current_direction = steepest_descent;
                    (*current_direction)(c);
                    c.dphi_0() = BackendType::dot(N,c.p(),c.g());
                }

                (*line_search)(search_res, current_direction.get(), c);

                if(search_res.has_failed)
                    return terminate(optimization_result::LINE_SEARCH_FAILED, res, N, c);

                c.alpha() = search_res.best_alpha;

                BackendType::copy(N,c.x(),c.xm1());
                BackendType::copy(N,search_res.best_x,c.x());

                BackendType::copy(N,c.g(),c.gm1());
                BackendType::copy(N,search_res.best_g,c.g());

                c.valm1() = c.val();
                c.val() = search_res.best_phi;

                if((*stopping_criterion)(c)){
                    return terminate(optimization_result::STOPPING_CRITERION, res, N, c);
                }
                current_direction = direction;

                if(model->update(c))
                  c.fun().compute_value_gradient(c.x(), c.val(), c.g(), c.model().get_value_gradient_tag());
            }

            return terminate(optimization_result::MAX_ITERATION_REACHED, res, N, c);
        }
    };


}

#endif
