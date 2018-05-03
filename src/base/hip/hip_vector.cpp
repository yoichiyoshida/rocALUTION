#include "../../utils/def.hpp"
#include "hip_vector.hpp"
#include "../base_vector.hpp"
#include "../host/host_vector.hpp"
#include "../backend_manager.hpp"
#include "../../utils/log.hpp"
#include "../../utils/allocate_free.hpp"
#include "../../utils/math_functions.hpp"
#include "hip_utils.hpp"
#include "hip_kernels_general.hpp"
#include "hip_kernels_vector.hpp"
#include "hip_allocate_free.hpp"
#include "hip_blas.hpp"

#include <hip/hip_runtime.h>

namespace rocalution {

template <typename ValueType>
HIPAcceleratorVector<ValueType>::HIPAcceleratorVector() {

  // no default constructors
  LOG_INFO("no default constructor");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
HIPAcceleratorVector<ValueType>::HIPAcceleratorVector(const Rocalution_Backend_Descriptor local_backend) {

  LOG_DEBUG(this, "HIPAcceleratorVector::HIPAcceleratorVector()",
            "constructor with local_backend");

  this->vec_ = NULL;
  this->set_backend(local_backend);

  this->index_array_  = NULL;
  this->index_buffer_ = NULL;

  this->host_buffer_ = NULL;
  this->device_buffer_ = NULL;

  CHECK_HIP_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
HIPAcceleratorVector<ValueType>::~HIPAcceleratorVector() {

  LOG_DEBUG(this, "HIPAcceleratorVector::~HIPAcceleratorVector()",
            "destructor");

  this->Clear();

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::info(void) const {

  LOG_INFO("HIPAcceleratorVector<ValueType>");

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::Allocate(const int n) {

  assert(n >= 0);

  if (this->get_size() >0)
    this->Clear();

  if (n > 0) {

    allocate_hip(n, &this->vec_);
    set_to_zero_hip(this->local_backend_.HIP_block_size, 
                    this->local_backend_.HIP_max_threads,
                    n, this->vec_);

    allocate_host(this->local_backend_.HIP_warp, &this->host_buffer_);
    allocate_hip(this->local_backend_.HIP_warp, &this->device_buffer_);

    this->size_ = n;
  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::SetDataPtr(ValueType **ptr, const int size) {

  assert(*ptr != NULL);
  assert(size > 0);

  hipDeviceSynchronize();

  this->vec_ = *ptr;
  this->size_ = size;

  allocate_host(this->local_backend_.HIP_warp, &this->host_buffer_);
  allocate_hip(this->local_backend_.HIP_warp, &this->device_buffer_);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::LeaveDataPtr(ValueType **ptr) {

  assert(this->get_size() > 0);

  hipDeviceSynchronize();
  *ptr = this->vec_;
  this->vec_ = NULL;

  free_host(&this->host_buffer_);
  free_hip(&this->device_buffer_);

  this->size_ = 0;

}


template <typename ValueType>
void HIPAcceleratorVector<ValueType>::Clear(void) {
  
  if (this->get_size() > 0) {

    free_hip(&this->vec_);
    this->size_ = 0;

  }

  if (this->index_size_ > 0) {

    free_hip(&this->index_buffer_);
    free_hip(&this->index_array_);
    this->index_size_ = 0;

    free_host(&this->host_buffer_);
    free_hip(&this->device_buffer_);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyFromHost(const HostVector<ValueType> &src) {

  // CPU to HIP copy
  const HostVector<ValueType> *cast_vec;
  if ((cast_vec = dynamic_cast<const HostVector<ValueType>*> (&src)) != NULL) {

    if (this->get_size() == 0) {

      // Allocate local structure
      this->Allocate(cast_vec->get_size());

      // Check for boundary
      assert(this->index_size_ == 0);
      if (cast_vec->index_size_ > 0) {

        this->index_size_ = cast_vec->index_size_;
        allocate_hip<int>(this->index_size_, &this->index_array_);
        allocate_hip<ValueType>(this->index_size_, &this->index_buffer_);

      }

    }

    assert(cast_vec->get_size() == this->get_size());
    assert(cast_vec->index_size_ == this->index_size_);

    if (this->get_size() > 0) {      

      hipMemcpy(this->vec_,
                cast_vec->vec_,
                this->size_*sizeof(ValueType),
                hipMemcpyHostToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);

      hipMemcpy(this->index_array_,
                cast_vec->index_array_,
                this->index_size_*sizeof(int),
                hipMemcpyHostToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);

    }

  } else {

    LOG_INFO("Error unsupported HIP vector type");
    this->info();
    src.info();
    FATAL_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyToHost(HostVector<ValueType> *dst) const {

  // HIP to CPU copy
  HostVector<ValueType> *cast_vec;
  if ((cast_vec = dynamic_cast<HostVector<ValueType>*> (dst)) != NULL) {

    if (cast_vec->get_size() == 0) {

      // Allocate local vector
      cast_vec->Allocate(this->get_size());

      // Check for boundary
      assert(cast_vec->index_size_ == 0);
      if (this->index_size_ > 0) {

        cast_vec->index_size_ = this->index_size_;
        allocate_host(this->index_size_, &cast_vec->index_array_);

      }

    }
      
    assert(cast_vec->get_size() == this->get_size());
    assert(cast_vec->index_size_ == this->index_size_);

    if (this->get_size() > 0) {

      hipMemcpy(cast_vec->vec_,
                this->vec_,
                this->size_*sizeof(ValueType),
                hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);

      hipMemcpy(cast_vec->index_array_,
                this->index_array_,
                this->index_size_*sizeof(int),
                hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);

    }

  } else {
    
    LOG_INFO("Error unsupported HIP vector type");
    this->info();
    dst->info();
    FATAL_ERROR(__FILE__, __LINE__);
    
  }

  
}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyFromHostAsync(const HostVector<ValueType> &src) {

  // CPU to HIP copy
  const HostVector<ValueType> *cast_vec;
  if ((cast_vec = dynamic_cast<const HostVector<ValueType>*> (&src)) != NULL) {

    if (this->get_size() == 0) {

      // Allocate local vector
      this->Allocate(cast_vec->get_size());

      // Check for boundary
      assert(this->index_size_ == 0);
      if (cast_vec->index_size_ > 0) {

        this->index_size_ = cast_vec->index_size_;
        allocate_hip<int>(this->index_size_, &this->index_array_);
        allocate_hip<ValueType>(this->index_size_, &this->index_buffer_);

      }

    }

    assert(cast_vec->get_size() == this->get_size());
    assert(cast_vec->index_size_ == this->index_size_);

    if (this->get_size() > 0) {

      hipMemcpyAsync(this->vec_,     // dst
                      cast_vec->vec_, // src
                      this->get_size()*sizeof(ValueType), // size
                      hipMemcpyHostToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);

      hipMemcpyAsync(this->index_array_,     // dst
                      cast_vec->index_array_, // src
                      this->index_size_*sizeof(int), // size
                      hipMemcpyHostToDevice);
      CHECK_HIP_ERROR(__FILE__, __LINE__);

    }

  } else {

    LOG_INFO("Error unsupported HIP vector type");
    this->info();
    src.info();
    FATAL_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyToHostAsync(HostVector<ValueType> *dst) const {

  // HIP to CPU copy
  HostVector<ValueType> *cast_vec;
  if ((cast_vec = dynamic_cast<HostVector<ValueType>*> (dst)) != NULL) {

    if (cast_vec->get_size() == 0) {

      // Allocate local vector
      cast_vec->Allocate(this->get_size());

      // Check for boundary
      assert(cast_vec->index_size_ == 0);
      if (this->index_size_ > 0) {

        cast_vec->index_size_ = this->index_size_;
        allocate_host(this->index_size_, &cast_vec->index_array_);

      }

    }

    assert(cast_vec->get_size() == this->get_size());
    assert(cast_vec->index_size_ == this->index_size_);

    if (this->get_size() > 0) {

      hipMemcpyAsync(cast_vec->vec_,  // dst
                      this->vec_,      // src
                      this->get_size()*sizeof(ValueType), // size
                      hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);

      hipMemcpyAsync(cast_vec->index_array_,  // dst
                      this->index_array_,      // src
                      this->index_size_*sizeof(int), // size
                      hipMemcpyDeviceToHost);
      CHECK_HIP_ERROR(__FILE__, __LINE__);

    }

  } else {

    LOG_INFO("Error unsupported HIP vector type");
    this->info();
    dst->info();
    FATAL_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyFrom(const BaseVector<ValueType> &src) {

  const HIPAcceleratorVector<ValueType> *hip_cast_vec;
  const HostVector<ValueType> *host_cast_vec;

  // HIP to HIP copy
  if ((hip_cast_vec = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&src)) != NULL) {

    if (this->get_size() == 0) {

      // Allocate local vector
      this->Allocate(hip_cast_vec->get_size());

      // Check for boundary
      assert(this->index_size_ == 0);
      if (hip_cast_vec->index_size_ > 0) {

        this->index_size_ = hip_cast_vec->index_size_;
        allocate_hip<int>(this->index_size_, &this->index_array_);
        allocate_hip<ValueType>(this->index_size_, &this->index_buffer_);

      }

    }

    assert(hip_cast_vec->get_size() == this->get_size());
    assert(hip_cast_vec->index_size_ == this->index_size_);

    if (this != hip_cast_vec)  {  

      if (this->get_size() > 0) {

        hipMemcpy(this->vec_,         // dst
                   hip_cast_vec->vec_, // src
                   this->get_size()*sizeof(ValueType), // size
                   hipMemcpyDeviceToDevice);
        CHECK_HIP_ERROR(__FILE__, __LINE__);

        hipMemcpy(this->index_array_,            // dst
                   hip_cast_vec->index_array_,    // src
                   this->index_size_*sizeof(int), // size
                   hipMemcpyDeviceToDevice);
        CHECK_HIP_ERROR(__FILE__, __LINE__);

      }

    }

  } else {
    
    //HIP to CPU copy
    if ((host_cast_vec = dynamic_cast<const HostVector<ValueType>*> (&src)) != NULL) {
      

      this->CopyFromHost(*host_cast_vec);
      
    
    } else {

      LOG_INFO("Error unsupported HIP vector type");
      this->info();
      src.info();
      FATAL_ERROR(__FILE__, __LINE__);
      
    }
    
  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyFromAsync(const BaseVector<ValueType> &src) {

  const HIPAcceleratorVector<ValueType> *hip_cast_vec;
  const HostVector<ValueType> *host_cast_vec;

  // HIP to HIP copy
  if ((hip_cast_vec = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&src)) != NULL) {

    if (this->get_size() == 0) {

      // Allocate local vector
      this->Allocate(hip_cast_vec->get_size());

      // Check for boundary
      assert(this->index_size_ == 0);
      if (hip_cast_vec->index_size_ > 0) {

        this->index_size_ = hip_cast_vec->index_size_;
        allocate_hip<int>(this->index_size_, &this->index_array_);
        allocate_hip<ValueType>(this->index_size_, &this->index_buffer_);

      }

    }

    assert(hip_cast_vec->get_size() == this->get_size());
    assert(hip_cast_vec->index_size_ == this->index_size_);

    if (this != hip_cast_vec) {

      if (this->get_size() > 0) {

        hipMemcpy(this->vec_,         // dst
                   hip_cast_vec->vec_, // src
                   this->get_size()*sizeof(ValueType), // size
                   hipMemcpyDeviceToDevice);
        CHECK_HIP_ERROR(__FILE__, __LINE__);

        hipMemcpy(this->index_array_,         // dst
                   hip_cast_vec->index_array_, // src
                   this->index_size_*sizeof(int), // size
                   hipMemcpyDeviceToDevice);
        CHECK_HIP_ERROR(__FILE__, __LINE__);

      }

    }

  } else {

    //HIP to CPU copy
    if ((host_cast_vec = dynamic_cast<const HostVector<ValueType>*> (&src)) != NULL) {

      this->CopyFromHostAsync(*host_cast_vec);

    } else {

      LOG_INFO("Error unsupported HIP vector type");
      this->info();
      src.info();
      FATAL_ERROR(__FILE__, __LINE__);

    }

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyFrom(const BaseVector<ValueType> &src,
                                               const int src_offset,
                                               const int dst_offset,
                                               const int size) {

//TODO  assert(&src != this);
  assert(this->get_size() > 0);
  assert(src.  get_size() > 0);
  assert(size > 0);

  assert(src_offset + size <= src.get_size());
  assert(dst_offset + size <= this->get_size());

  const HIPAcceleratorVector<ValueType> *cast_src = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&src);
  assert(cast_src != NULL);

  dim3 BlockSize(this->local_backend_.HIP_block_size);
  dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);

  hipLaunchKernelGGL((kernel_copy_offset_from<ValueType, int>),
                     GridSize, BlockSize, 0, 0,
                     size, src_offset, dst_offset,
                     cast_src->vec_, this->vec_);
  CHECK_HIP_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyTo(BaseVector<ValueType> *dst) const{

  HIPAcceleratorVector<ValueType> *hip_cast_vec;
  HostVector<ValueType> *host_cast_vec;

    // HIP to HIP copy
    if ((hip_cast_vec = dynamic_cast<HIPAcceleratorVector<ValueType>*> (dst)) != NULL) {

      if (hip_cast_vec->get_size() == 0) {

        // Allocate local vector
        hip_cast_vec->Allocate(this->get_size());

        // Check for boundary
        assert(hip_cast_vec->index_size_ == 0);
        if (this->index_size_ > 0) {

          hip_cast_vec->index_size_ = this->index_size_;
          allocate_hip<int>(this->index_size_, &hip_cast_vec->index_array_);
          allocate_hip<ValueType>(this->index_size_, &hip_cast_vec->index_buffer_);

        }

      }

      assert(hip_cast_vec->get_size() == this->get_size());
      assert(hip_cast_vec->index_size_ == this->index_size_);

      if (this != hip_cast_vec)  {

        if (this->get_size() >0) {

          hipMemcpy(hip_cast_vec->vec_, // dst
                     this->vec_,         // src
                     this->get_size()*sizeof(ValueType), // size
                     hipMemcpyDeviceToDevice);
          CHECK_HIP_ERROR(__FILE__, __LINE__);

          hipMemcpy(hip_cast_vec->index_array_,    // dst
                     this->index_array_,            // src
                     this->index_size_*sizeof(int), // size
                     hipMemcpyDeviceToDevice);
          CHECK_HIP_ERROR(__FILE__, __LINE__);

        }
      }

    } else {
      
      //HIP to CPU copy
      if ((host_cast_vec = dynamic_cast<HostVector<ValueType>*> (dst)) != NULL) {
        

        this->CopyToHost(host_cast_vec);
        
      
      } else {

        LOG_INFO("Error unsupported HIP vector type");
        this->info();
        dst->info();
        FATAL_ERROR(__FILE__, __LINE__);
        
      }
      
    }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyToAsync(BaseVector<ValueType> *dst) const {

  HIPAcceleratorVector<ValueType> *hip_cast_vec;
  HostVector<ValueType> *host_cast_vec;

  // HIP to HIP copy
  if ((hip_cast_vec = dynamic_cast<HIPAcceleratorVector<ValueType>*> (dst)) != NULL) {

    if (hip_cast_vec->get_size() == 0) {

      // Allocate local vector
      hip_cast_vec->Allocate(this->get_size());

      // Check for boundary
      assert(hip_cast_vec->index_size_ == 0);
      if (this->index_size_ > 0) {

        hip_cast_vec->index_size_ = this->index_size_;
        allocate_hip<int>(this->index_size_, &hip_cast_vec->index_array_);
        allocate_hip<ValueType>(this->index_size_, &hip_cast_vec->index_buffer_);

      }

    }

    assert(hip_cast_vec->get_size() == this->get_size());
    assert(hip_cast_vec->index_size_ == this->index_size_);

    if (this != hip_cast_vec) {

      if (this->get_size() > 0) {

        hipMemcpy(hip_cast_vec->vec_, // dst
                   this->vec_,         // src
                   this->get_size()*sizeof(ValueType), // size
                   hipMemcpyDeviceToDevice);
        CHECK_HIP_ERROR(__FILE__, __LINE__);

        hipMemcpy(hip_cast_vec->index_array_, // dst
                   this->index_array_,         // src
                   this->index_size_*sizeof(int), // size
                   hipMemcpyDeviceToDevice);
        CHECK_HIP_ERROR(__FILE__, __LINE__);

      }

    }

  } else {

    //HIP to CPU copy
    if ((host_cast_vec = dynamic_cast<HostVector<ValueType>*> (dst)) != NULL) {

      this->CopyToHostAsync(host_cast_vec);

    } else {

      LOG_INFO("Error unsupported HIP vector type");
      this->info();
      dst->info();
      FATAL_ERROR(__FILE__, __LINE__);

    }

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyFromFloat(const BaseVector<float> &src) {

  LOG_INFO("Mixed precision for non-complex to complex casting is not allowed");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <>
void HIPAcceleratorVector<double>::CopyFromFloat(const BaseVector<float> &src) {

  const HIPAcceleratorVector<float> *hip_cast_vec;

  // HIP to HIP copy
  if ((hip_cast_vec = dynamic_cast<const HIPAcceleratorVector<float>*> (&src)) != NULL) {

    if (this->get_size() == 0)
      this->Allocate(hip_cast_vec->get_size());

    assert(hip_cast_vec->get_size() == this->get_size());

    if (this->get_size() > 0) {

      dim3 BlockSize(this->local_backend_.HIP_block_size);
      dim3 GridSize(this->get_size() / this->local_backend_.HIP_block_size + 1);

      hipLaunchKernelGGL((kernel_copy_from_float<double, int>),
                         GridSize, BlockSize, 0, 0,
                         this->get_size(), hip_cast_vec->vec_, this->vec_);
      CHECK_HIP_ERROR(__FILE__, __LINE__);

    }

  } else {

    LOG_INFO("Error unsupported HIP vector type");
    FATAL_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyFromDouble(const BaseVector<double> &src) {

  LOG_INFO("Mixed precision for non-complex to complex casting is not allowed");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <>
void HIPAcceleratorVector<float>::CopyFromDouble(const BaseVector<double> &src) {

  const HIPAcceleratorVector<double> *hip_cast_vec;

  // HIP to HIP copy
  if ((hip_cast_vec = dynamic_cast<const HIPAcceleratorVector<double>*> (&src)) != NULL) {

    if (this->get_size() == 0)
      this->Allocate(hip_cast_vec->get_size());

    assert(hip_cast_vec->get_size() == this->get_size());

    if (this->get_size()  >0) {

      dim3 BlockSize(this->local_backend_.HIP_block_size);
      dim3 GridSize(this->get_size() / this->local_backend_.HIP_block_size + 1);

      hipLaunchKernelGGL((kernel_copy_from_double<float, int>),
                         GridSize, BlockSize, 0, 0,
                         this->get_size(), hip_cast_vec->vec_, this->vec_);
      CHECK_HIP_ERROR(__FILE__, __LINE__);
    }

  } else {
    LOG_INFO("Error unsupported HIP vector type");
    FATAL_ERROR(__FILE__, __LINE__);

  }
  
}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyFromData(const ValueType *data) {

  if (this->get_size() > 0) {

    hipMemcpy(this->vec_,                         // dst
               data,                               // src
               this->get_size()*sizeof(ValueType), // size
               hipMemcpyDeviceToDevice);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyToData(ValueType *data) const {

  if (this->get_size() > 0) {

    hipMemcpy(data,                               // dst
               this->vec_,                         // src
               this->get_size()*sizeof(ValueType), // size
               hipMemcpyDeviceToDevice);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::Zeros(void) {

  if (this->get_size() > 0) {

    set_to_zero_hip(this->local_backend_.HIP_block_size,
                    this->local_backend_.HIP_max_threads,
                    this->get_size(), this->vec_);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::Ones(void) {

  if (this->get_size() > 0)
    set_to_one_hip(this->local_backend_.HIP_block_size, 
                   this->local_backend_.HIP_max_threads,
                   this->get_size(), this->vec_);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::SetValues(const ValueType val) {

  LOG_INFO("HIPAcceleratorVector::SetValues NYI");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::AddScale(const BaseVector<ValueType> &x, const ValueType alpha) {

  if (this->get_size() > 0) {

    assert(this->get_size() == x.get_size());
    
    const HIPAcceleratorVector<ValueType> *cast_x = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&x);
    assert(cast_x != NULL);

    hipblasStatus_t stat_t;
    stat_t = hipblasTaxpy(HIPBLAS_HANDLE(this->local_backend_.HIP_blas_handle), 
                          this->get_size(), &alpha, cast_x->vec_, 1, this->vec_, 1);
    CHECK_HIPBLAS_ERROR(stat_t, __FILE__, __LINE__);

  }

}

template <>
void HIPAcceleratorVector<int>::AddScale(const BaseVector<int> &x, const int alpha) {

  LOG_INFO("No int axpy function");
  FATAL_ERROR(__FILE__, __LINE__);
 
}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::ScaleAdd(const ValueType alpha, const BaseVector<ValueType> &x) {

  if (this->get_size() > 0) {

    assert(this->get_size() == x.get_size());

    const HIPAcceleratorVector<ValueType> *cast_x = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&x);
    assert(cast_x != NULL);

    int size = this->get_size();
    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);

    hipLaunchKernelGGL((kernel_scaleadd<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       size, HIPVal(alpha), HIPPtr(cast_x->vec_), HIPPtr(this->vec_));
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::ScaleAddScale(const ValueType alpha, const BaseVector<ValueType> &x, const ValueType beta) {

  if (this->get_size() > 0) {

    assert(this->get_size() == x.get_size());

    const HIPAcceleratorVector<ValueType> *cast_x = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&x);
    assert(cast_x != NULL);

    int size = this->get_size();
    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);

    hipLaunchKernelGGL((kernel_scaleaddscale<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       size, HIPVal(alpha), HIPVal(beta),
                       HIPPtr(cast_x->vec_), HIPPtr(this->vec_));
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::ScaleAddScale(const ValueType alpha, const BaseVector<ValueType> &x, const ValueType beta,
                                                    const int src_offset, const int dst_offset,const int size) {

  if (this->get_size() > 0) {

    assert(this->get_size() > 0);
    assert(x.get_size() > 0);
    assert(size > 0);
    assert(src_offset + size <= x.get_size());
    assert(dst_offset + size <= this->get_size());

    const HIPAcceleratorVector<ValueType> *cast_x = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&x);
    assert(cast_x != NULL);

    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);

    hipLaunchKernelGGL((kernel_scaleaddscale_offset<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       size, src_offset, dst_offset,
                       HIPVal(alpha), HIPVal(beta),
                       HIPPtr(cast_x->vec_), HIPPtr(this->vec_));
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::ScaleAdd2(const ValueType alpha, const BaseVector<ValueType> &x,
                                                const ValueType beta, const BaseVector<ValueType> &y,
                                                const ValueType gamma) {

  if (this->get_size() > 0) {

    assert(this->get_size() == x.get_size());
    assert(this->get_size() == y.get_size());

    const HIPAcceleratorVector<ValueType> *cast_x = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&x);
    const HIPAcceleratorVector<ValueType> *cast_y = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&y);
    assert(cast_x != NULL);
    assert(cast_y != NULL);

    int size = this->get_size();
    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);

    hipLaunchKernelGGL((kernel_scaleadd2<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       size, HIPVal(alpha), HIPVal(beta), HIPVal(gamma),
                       HIPPtr(cast_x->vec_), HIPPtr(cast_y->vec_), HIPPtr(this->vec_));
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::Scale(const ValueType alpha) {

  if (this->get_size() > 0) {

    hipblasStatus_t stat_t;
    stat_t = hipblasTscal(HIPBLAS_HANDLE(this->local_backend_.HIP_blas_handle),
                          this->get_size(), &alpha,
                          this->vec_, 1);
    CHECK_HIPBLAS_ERROR(stat_t, __FILE__, __LINE__);

  }

}

template <>
void HIPAcceleratorVector<int>::Scale(const int alpha) {

  LOG_INFO("No int CUBLAS scale function");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::ExclusiveScan(const BaseVector<ValueType> &x) {

  LOG_INFO("HIPAcceleratorVector::ExclusiveScan() NYI");
  FATAL_ERROR(__FILE__, __LINE__); 

}

template <typename ValueType>
ValueType HIPAcceleratorVector<ValueType>::Dot(const BaseVector<ValueType> &x) const {

  assert(this->get_size() == x.get_size());

  const HIPAcceleratorVector<ValueType> *cast_x = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&x);
  assert(cast_x != NULL);

  ValueType res = static_cast<ValueType>(0);

  if (this->get_size() > 0) {

    hipblasStatus_t stat_t;
    stat_t = hipblasTdot(HIPBLAS_HANDLE(this->local_backend_.HIP_blas_handle),
                         this->get_size(), this->vec_, 1, cast_x->vec_, 1, &res);
    CHECK_HIPBLAS_ERROR(stat_t, __FILE__, __LINE__);

  }

  return res;

}

template <>
int HIPAcceleratorVector<int>::Dot(const BaseVector<int> &x) const {

  LOG_INFO("No int dot function");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
ValueType HIPAcceleratorVector<ValueType>::DotNonConj(const BaseVector<ValueType> &x) const {

  assert(this->get_size() == x.get_size());

  const HIPAcceleratorVector<ValueType> *cast_x = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&x);
  assert(cast_x != NULL);

  ValueType res = static_cast<ValueType>(0);

  if (this->get_size() > 0) {

      hipblasStatus_t stat_t;
      stat_t = hipblasTdotc(HIPBLAS_HANDLE(this->local_backend_.HIP_blas_handle),
                            this->get_size(), this->vec_, 1, cast_x->vec_, 1, &res);
      CHECK_HIPBLAS_ERROR(stat_t, __FILE__, __LINE__);

  }

  return res;

}

template <>
int HIPAcceleratorVector<int>::DotNonConj(const BaseVector<int> &x) const {

  LOG_INFO("No int dotc function");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
ValueType HIPAcceleratorVector<ValueType>::Norm(void) const {

  ValueType res = static_cast<ValueType>(0);

  if (this->get_size() > 0) {

    hipblasStatus_t stat_t;
    stat_t = hipblasTnrm2(HIPBLAS_HANDLE(this->local_backend_.HIP_blas_handle),
                          this->get_size(), this->vec_, 1, &res);
    CHECK_HIPBLAS_ERROR(stat_t, __FILE__, __LINE__);

  }

  return res;

}

template <>
int HIPAcceleratorVector<int>::Norm(void) const {

  LOG_INFO("What is int HIPAcceleratorVector<ValueType>::Norm(void) const?");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
ValueType HIPAcceleratorVector<ValueType>::Reduce(void) const {

  ValueType res = (ValueType) 0;

  if (this->get_size() > 0) {

    if (this->local_backend_.HIP_warp == 32) {
        reduce_hip<int, ValueType, 32, 256>(this->get_size(), this->vec_, &res, this->host_buffer_, this->device_buffer_);
    } else if (this->local_backend_.HIP_warp == 64) {
        reduce_hip<int, ValueType, 64, 256>(this->get_size(), this->vec_, &res, this->host_buffer_, this->device_buffer_);
    } else {
        LOG_INFO("Unsupported warp size");
        FATAL_ERROR(__FILE__, __LINE__);
    }
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

  return res;

}

template <>
int HIPAcceleratorVector<int>::Reduce(void) const {

  LOG_INFO("Reduce<int> not implemented");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
ValueType HIPAcceleratorVector<ValueType>::Asum(void) const {

  ValueType res = static_cast<ValueType>(0);

  if (this->get_size() > 0) {

    hipblasStatus_t stat_t;
    stat_t = hipblasTasum(HIPBLAS_HANDLE(this->local_backend_.HIP_blas_handle),
                          this->get_size(), this->vec_, 1, &res);
    CHECK_HIPBLAS_ERROR(stat_t, __FILE__, __LINE__);

  }

  return res;

}

template <>
int HIPAcceleratorVector<int>::Asum(void) const {

  LOG_INFO("Asum<int> not implemented");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
int HIPAcceleratorVector<ValueType>::Amax(ValueType &value) const {

  int index = 0;
  value = static_cast<ValueType>(0.0);

  if (this->get_size() > 0) {

    hipblasStatus_t stat_t;
    stat_t = hipblasTamax(HIPBLAS_HANDLE(this->local_backend_.HIP_blas_handle),
                          this->get_size(),
                          this->vec_, 1, &index);
    CHECK_HIPBLAS_ERROR(stat_t, __FILE__, __LINE__);

    hipMemcpy(&value, this->vec_+index, sizeof(ValueType), hipMemcpyDeviceToHost);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

  value = rocalution_abs(value);
  return index;

}

template <>
int HIPAcceleratorVector<int>::Amax(int &value) const {

  LOG_INFO("Amax<int> not implemented");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::PointWiseMult(const BaseVector<ValueType> &x) {

  if (this->get_size() > 0) {

    assert(this->get_size() == x.get_size());

    const HIPAcceleratorVector<ValueType> *cast_x = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&x);
    assert(cast_x != NULL);

    int size = this->get_size();
    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);

    hipLaunchKernelGGL((kernel_pointwisemult<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       size, HIPPtr(cast_x->vec_), HIPPtr(this->vec_));
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::PointWiseMult(const BaseVector<ValueType> &x, const BaseVector<ValueType> &y) {

  if (this->get_size() > 0) {

    assert(this->get_size() == x.get_size());
    assert(this->get_size() == y.get_size());

    const HIPAcceleratorVector<ValueType> *cast_x = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&x);
    const HIPAcceleratorVector<ValueType> *cast_y = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&y);
    assert(cast_x != NULL);
    assert(cast_y != NULL);

    int size = this->get_size();
    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);

    hipLaunchKernelGGL((kernel_pointwisemult2<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       size, HIPPtr(cast_x->vec_), HIPPtr(cast_y->vec_),
                       HIPPtr(this->vec_));
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::Permute(const BaseVector<int> &permutation) {

  if (this->get_size() > 0) {

    assert(this->get_size() == permutation.get_size());
    
    const HIPAcceleratorVector<int> *cast_perm = dynamic_cast<const HIPAcceleratorVector<int>*> (&permutation);
    assert(cast_perm != NULL);
    
    HIPAcceleratorVector<ValueType> vec_tmp(this->local_backend_);     
    vec_tmp.Allocate(this->get_size());
    vec_tmp.CopyFrom(*this);
    
    int size = this->get_size();
    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);
    
    // this->vec_[ cast_perm->vec_[i] ] = vec_tmp.vec_[i];  
    hipLaunchKernelGGL((kernel_permute<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       size, cast_perm->vec_, vec_tmp.vec_, this->vec_);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::PermuteBackward(const BaseVector<int> &permutation) {

  if (this->get_size() > 0) {

    assert(this->get_size() == permutation.get_size());
    
    const HIPAcceleratorVector<int> *cast_perm = dynamic_cast<const HIPAcceleratorVector<int>*> (&permutation);
    assert(cast_perm != NULL);
    
    HIPAcceleratorVector<ValueType> vec_tmp(this->local_backend_);   
    vec_tmp.Allocate(this->get_size());
    vec_tmp.CopyFrom(*this);
    
    int size = this->get_size();
    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);
    
    //    this->vec_[i] = vec_tmp.vec_[ cast_perm->vec_[i] ];
    hipLaunchKernelGGL((kernel_permute_backward<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       size, cast_perm->vec_, vec_tmp.vec_, this->vec_);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyFromPermute(const BaseVector<ValueType> &src,
                                                      const BaseVector<int> &permutation) { 

  if (this->get_size() > 0) {

//TODO    assert(this != &src);
    
    const HIPAcceleratorVector<ValueType> *cast_vec = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&src);
    const HIPAcceleratorVector<int> *cast_perm      = dynamic_cast<const HIPAcceleratorVector<int>*> (&permutation) ; 
    assert(cast_perm != NULL);
    assert(cast_vec  != NULL);
    
    assert(cast_vec ->get_size() == this->get_size());
    assert(cast_perm->get_size() == this->get_size());
    
    int size = this->get_size();
    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);
    
    //    this->vec_[ cast_perm->vec_[i] ] = cast_vec->vec_[i];
    hipLaunchKernelGGL((kernel_permute<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       size, cast_perm->vec_, cast_vec->vec_, this->vec_);    
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::CopyFromPermuteBackward(const BaseVector<ValueType> &src,
                                                              const BaseVector<int> &permutation) {

  if (this->get_size() > 0) {

//TODO    assert(this != &src);
    
    const HIPAcceleratorVector<ValueType> *cast_vec = dynamic_cast<const HIPAcceleratorVector<ValueType>*> (&src);
    const HIPAcceleratorVector<int> *cast_perm      = dynamic_cast<const HIPAcceleratorVector<int>*> (&permutation) ; 
    assert(cast_perm != NULL);
    assert(cast_vec  != NULL);
    
    assert(cast_vec ->get_size() == this->get_size());
    assert(cast_perm->get_size() == this->get_size());
    
    
    int size = this->get_size();
    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);
    
    //    this->vec_[i] = cast_vec->vec_[ cast_perm->vec_[i] ];
    hipLaunchKernelGGL((kernel_permute_backward<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       size, cast_perm->vec_, cast_vec->vec_, this->vec_);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::SetIndexArray(const int size, const int *index) {

  assert(size > 0);
  assert(this->get_size() >= size);

  this->index_size_ = size;

  allocate_hip<int>(this->index_size_, &this->index_array_);
  allocate_hip<ValueType>(this->index_size_, &this->index_buffer_);

  hipMemcpy(this->index_array_,            // dst
             index,                         // src
             this->index_size_*sizeof(int), // size
             hipMemcpyHostToDevice);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::GetIndexValues(ValueType *values) const {

  assert(values != NULL);

  dim3 BlockSize(this->local_backend_.HIP_block_size);
  dim3 GridSize(this->index_size_ / this->local_backend_.HIP_block_size + 1);

  hipLaunchKernelGGL((kernel_get_index_values<ValueType, int>),
                     GridSize, BlockSize, 0, 0,
                     this->index_size_, this->index_array_,
                     this->vec_, this->index_buffer_);
  CHECK_HIP_ERROR(__FILE__, __LINE__);

  hipMemcpy(values,                              // dst
            this->index_buffer_,                 // src
            this->index_size_*sizeof(ValueType), // size
            hipMemcpyDeviceToHost);
  CHECK_HIP_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::SetIndexValues(const ValueType *values) {

  assert(values != NULL);

  hipMemcpy(this->index_buffer_,
            values,
            this->index_size_*sizeof(ValueType),
            hipMemcpyHostToDevice);
  CHECK_HIP_ERROR(__FILE__, __LINE__);

  dim3 BlockSize(this->local_backend_.HIP_block_size);
  dim3 GridSize(this->index_size_ / this->local_backend_.HIP_block_size + 1);

  hipLaunchKernelGGL((kernel_set_index_values<ValueType, int>),
                     GridSize, BlockSize, 0, 0,
                     this->index_size_, this->index_array_,
                     this->index_buffer_, this->vec_);
  CHECK_HIP_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::GetContinuousValues(const int start, const int end, ValueType *values) const {

  assert(start >= 0);
  assert(end >= start);
  assert(end <= this->get_size());
  assert(values != NULL);

  hipMemcpy(values,                        // dst
            this->vec_+start,              // src
            (end-start)*sizeof(ValueType), // size
            hipMemcpyDeviceToHost);
  CHECK_HIP_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::SetContinuousValues(const int start, const int end, const ValueType *values) {

  assert(start >= 0);
  assert(end >= start);
  assert(end <= this->get_size());
  assert(values != NULL);

  hipMemcpy(this->vec_+start,              // dst
            values,                        // src
            (end-start)*sizeof(ValueType), // size
            hipMemcpyHostToDevice);
  CHECK_HIP_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::ExtractCoarseMapping(const int start, const int end, const int *index,
                                                           const int nc, int *size, int *map) const {

  LOG_INFO("ExtractCoarseMapping() NYI for HIP");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::ExtractCoarseBoundary(const int start, const int end, const int *index,
                                                            const int nc, int *size, int *boundary) const {

  LOG_INFO("ExtractCoarseBoundary() NYI for HIP");
  FATAL_ERROR(__FILE__, __LINE__);

}

template <typename ValueType>
void HIPAcceleratorVector<ValueType>::Power(const double power) {

  if (this->get_size() > 0) {

    int size = this->get_size();
    dim3 BlockSize(this->local_backend_.HIP_block_size);
    dim3 GridSize(size / this->local_backend_.HIP_block_size + 1);

    hipLaunchKernelGGL((kernel_power<ValueType, int>),
                       GridSize, BlockSize, 0, 0,
                       size, power, this->vec_);
    CHECK_HIP_ERROR(__FILE__, __LINE__);

  }

}

template <>
void HIPAcceleratorVector<int>::Power(const double power) {

  if (this->get_size() > 0) {

    LOG_INFO("HIPAcceleratorVector::Power(), no pow() for int in HIP");
    FATAL_ERROR(__FILE__, __LINE__);


  }

}


template class HIPAcceleratorVector<double>;
template class HIPAcceleratorVector<float>;
#ifdef SUPPORT_COMPLEX
template class HIPAcceleratorVector<std::complex<double> >;
template class HIPAcceleratorVector<std::complex<float> >;
#endif
template class HIPAcceleratorVector<int>;

}
