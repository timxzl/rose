 module boundary

   type, public :: bndy
     integer  :: nmsg        
     integer , dimension(:), pointer ::  nblocks  
   end type bndy

contains

 subroutine foo (newbndy)

   type (bndy), intent(out) :: newbndy   

   allocate (newbndy%nblocks(newbndy%nmsg))

 end subroutine foo

end module boundary
