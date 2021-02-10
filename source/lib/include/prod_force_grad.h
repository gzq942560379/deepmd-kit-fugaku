#pragma once

template<typename FPTYPE>
void prod_force_grad_a_cpu(
    FPTYPE * grad_net, 
    const FPTYPE * grad, 
    const FPTYPE * env_deriv, 
    const int * nlist, 
    const int nloc, 
    const int nnei);

