 module derived_array

   type, public :: distrb 
      integer , dimension(:), pointer :: proc             
   end type

contains

 subroutine foo(dist)

   type (distrb) :: dist           

   integer :: n 
   dist%proc(n) = 0  

 end subroutine foo

end module
