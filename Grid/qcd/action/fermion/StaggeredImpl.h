/*************************************************************************************

Grid physics library, www.github.com/paboyle/Grid

Source file: ./lib/qcd/action/fermion/FermionOperatorImpl.h

Copyright (C) 2015

Author: Peter Boyle <pabobyle@ph.ed.ac.uk>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

See the full license in the file "LICENSE" in the top level distribution
directory
*************************************************************************************/
			   /*  END LEGAL */
#pragma once

NAMESPACE_BEGIN(Grid);

template <class S, class Representation = FundamentalRepresentation >
class StaggeredImpl : public PeriodicGaugeImpl<GaugeImplTypes<S, Representation::Dimension > > 
{

public:

  typedef RealD  _Coeff_t ;
  static const int Dimension = Representation::Dimension;
  static const bool isFundamental = Representation::isFundamental;
  static const bool LsVectorised=false;
  typedef PeriodicGaugeImpl<GaugeImplTypes<S, Dimension > > Gimpl;
      
  //Necessary?
  constexpr bool is_fundamental() const{return Dimension == Nc ? 1 : 0;}
    
  typedef _Coeff_t Coeff_t;

  INHERIT_GIMPL_TYPES(Gimpl);
      
  template <typename vtype> using iImplSpinor            = iScalar<iScalar<iVector<vtype, Dimension> > >;
  template <typename vtype> using iImplHalfSpinor        = iScalar<iScalar<iVector<vtype, Dimension> > >;
  template <typename vtype> using iImplDoubledGaugeField = iVector<iScalar<iMatrix<vtype, Dimension> >, Nds>;
  template <typename vtype> using iImplPropagator        = iScalar<iScalar<iMatrix<vtype, Dimension> > >;
    
  typedef iImplSpinor<Simd>            SiteSpinor;
  typedef iImplHalfSpinor<Simd>        SiteHalfSpinor;
  typedef iImplDoubledGaugeField<Simd> SiteDoubledGaugeField;
  typedef iImplPropagator<Simd>        SitePropagator;
    
  typedef Lattice<SiteSpinor>            FermionField;
  typedef Lattice<SiteDoubledGaugeField> DoubledGaugeField;
  typedef Lattice<SitePropagator> PropagatorField;
    
  typedef StaggeredImplParams ImplParams;
  typedef SimpleCompressor<SiteSpinor> Compressor;
  typedef CartesianStencil<SiteSpinor, SiteSpinor, ImplParams> StencilImpl;
  typedef typename StencilImpl::View_type StencilView;

  ImplParams Params;
    
  StaggeredImpl(const ImplParams &p = ImplParams()) : Params(p){
    assert(Params.boundary_phases.size() == Nd);
  };
      
  template<class _Spinor>
  static accelerator_inline void multLink(_Spinor &phi,
		       const SiteDoubledGaugeField &U,
		       const _Spinor &chi,
		       int mu)
  {
    auto UU = coalescedRead(U(mu));
    mult(&phi(), &UU, &chi());
  }
  template<class _Spinor>
  static accelerator_inline void multLinkAdd(_Spinor &phi,
			  const SiteDoubledGaugeField &U,
			  const _Spinor &chi,
			  int mu)
  {
    auto UU = coalescedRead(U(mu));
    mac(&phi(), &UU, &chi());
  }
      
  template <class ref>
  static accelerator_inline void loadLinkElement(Simd &reg, ref &memory) 
  {
    reg = memory;
  }
      
    inline void InsertGaugeField(DoubledGaugeField &U_ds,
				 const GaugeLinkField &U,int mu)
    {
      PokeIndex<LorentzIndex>(U_ds, U, mu);
    }

  inline void DoubleStore(GridBase *GaugeGrid,
			  DoubledGaugeField &UUUds, // for Naik term
			  DoubledGaugeField &Uds,
			  const GaugeField &Uthin,
			  const GaugeField &Ufat) {

    typedef typename Simd::scalar_type scalar_type;

    conformable(Uds.Grid(), GaugeGrid);
    conformable(Uthin.Grid(), GaugeGrid);
    conformable(Ufat.Grid(), GaugeGrid);
    GaugeLinkField U(GaugeGrid);
    GaugeLinkField UU(GaugeGrid);
    GaugeLinkField UUU(GaugeGrid);
    GaugeLinkField Udag(GaugeGrid);
    GaugeLinkField UUUdag(GaugeGrid);
    Lattice<iScalar<vInteger> > coor(GaugeGrid);
    Lattice<iScalar<vInteger> > x(GaugeGrid); LatticeCoordinate(x,0);
    Lattice<iScalar<vInteger> > y(GaugeGrid); LatticeCoordinate(y,1);
    Lattice<iScalar<vInteger> > z(GaugeGrid); LatticeCoordinate(z,2);
    Lattice<iScalar<vInteger> > t(GaugeGrid); LatticeCoordinate(t,3);
    Lattice<iScalar<vInteger> > lin_z(GaugeGrid); lin_z=x+y;
    Lattice<iScalar<vInteger> > lin_t(GaugeGrid); lin_t=x+y+z;
    ComplexField KSphases(GaugeGrid); 

    for (int mu = 0; mu < Nd; mu++) {

      int L   = GaugeGrid->GlobalDimensions()[mu];
      int Lmu = L - 1;

      KSphases = 1.0;

      LatticeCoordinate(coor, mu);

      ////////// boundary phase /////////////
      auto pha = Params.boundary_phases[mu];
      scalar_type bphase( real(pha),imag(pha) );

      // apply any twists
      RealD theta = Params.twist_n_2pi_L[mu] * 2*M_PI / L;
      if ( theta != 0.0) { 
        scalar_type twphase(::cos(theta),::sin(theta));

        std::cout << GridLogMessage << " Twist [" << mu << "] " << Params.twist_n_2pi_L[mu] << " phase"<< bphase << std::endl;

        bphase = bphase*twphase;
      }

      // Staggered Phase.
      if ( mu == 1 ) KSphases = where( mod(x    ,2)==(Integer)0, KSphases,-KSphases);
      if ( mu == 2 ) KSphases = where( mod(lin_z,2)==(Integer)0, KSphases,-KSphases);
      if ( mu == 3 ) KSphases = where( mod(lin_t,2)==(Integer)0, KSphases,-KSphases);

      // 1 hop based on fat links
      U      = PeekIndex<LorentzIndex>(Ufat, mu);
      Udag   = adj( Cshift(U, mu, -1));

      U = U * KSphases;
      U = where(coor == Lmu, bphase * U , U);

      Udag = Udag * KSphases;
      Udag = where(coor == 0, conjugate(bphase) * Udag , Udag);

      InsertGaugeField(Uds,U,mu);
      InsertGaugeField(Uds,Udag,mu+4);

      // 3 hop based on thin links. Crazy huh ?
      U  = PeekIndex<LorentzIndex>(Uthin, mu);
      UU = Gimpl::CovShiftForward(U,mu,U);
      UUU= Gimpl::CovShiftForward(U,mu,UU);
	
      UUUdag = adj( Cshift(UUU, mu, -3));

      UUU = UUU * KSphases;
      UUU = where(coor == Lmu, bphase * UUU , UUU);

      UUUdag = UUUdag * KSphases;
      UUUdag = where(coor == 0, conjugate(bphase) * UUUdag , UUUdag);

      InsertGaugeField(UUUds,UUU,mu);
      InsertGaugeField(UUUds,UUUdag,mu+4);

    }
  }

  inline void DoubleStore(GridBase *GaugeGrid,
                        DoubledGaugeField &UUUds, // for Naik term
                        DoubledGaugeField &Uds,
                        const GaugeField &Uthin,
                        const GaugeField &Ufat,
                        const GaugeField &Ulong)
    {

      typedef typename Simd::scalar_type scalar_type;

      // Note: This code makes no use of Uthin
      conformable(Uds.Grid(), GaugeGrid);
      conformable(Ufat.Grid(), GaugeGrid);
      conformable(Ulong.Grid(), GaugeGrid);
      GaugeLinkField U(GaugeGrid);
      GaugeLinkField UUU(GaugeGrid);
      GaugeLinkField Udag(GaugeGrid);
      GaugeLinkField UUUdag(GaugeGrid);
      Lattice<iScalar<vInteger> > coor(GaugeGrid);
      Lattice<iScalar<vInteger> > x(GaugeGrid); LatticeCoordinate(x,0);
      Lattice<iScalar<vInteger> > y(GaugeGrid); LatticeCoordinate(y,1);
      Lattice<iScalar<vInteger> > z(GaugeGrid); LatticeCoordinate(z,2);
      Lattice<iScalar<vInteger> > t(GaugeGrid); LatticeCoordinate(t,3);
      Lattice<iScalar<vInteger> > lin_z(GaugeGrid); lin_z=x+y;
      Lattice<iScalar<vInteger> > lin_t(GaugeGrid); lin_t=x+y+z;
      ComplexField KSphases(GaugeGrid);        

      for (int mu = 0; mu < Nd; mu++) {
          
        int L   = GaugeGrid->GlobalDimensions()[mu];
        int Lmu = L - 1;

         KSphases = 1.0;

        LatticeCoordinate(coor, mu);
            
        ////////// boundary phase /////////////
        auto pha = Params.boundary_phases[mu];
        scalar_type bphase( real(pha),imag(pha) );

        // apply any twists
        RealD theta = Params.twist_n_2pi_L[mu] * 2*M_PI / L;
        if ( theta != 0.0) { 
          scalar_type twphase(::cos(theta),::sin(theta));

          std::cout << GridLogMessage << " Twist [" << mu << "] " << Params.twist_n_2pi_L[mu] << " phase"<< bphase << std::endl;

          bphase = bphase*twphase;
        }

        // Staggered Phase.
      	if ( mu == 1 ) KSphases = where( mod(x    ,2)==(Integer)0, KSphases,-KSphases);
      	if ( mu == 2 ) KSphases = where( mod(lin_z,2)==(Integer)0, KSphases,-KSphases);
      	if ( mu == 3 ) KSphases = where( mod(lin_t,2)==(Integer)0, KSphases,-KSphases);
        
      	// 1 hop based on fat links
      	U      = PeekIndex<LorentzIndex>(Ufat, mu);
      	Udag   = adj( Cshift(U, mu, -1));
              
      	U = U * KSphases;
        U = where(coor == Lmu, bphase * U , U);

      	Udag = Udag * KSphases;
        Udag = where(coor == 0, conjugate(bphase) * Udag , Udag);
                  
      	InsertGaugeField(Uds,U,mu);
      	InsertGaugeField(Uds,Udag,mu+4);

        #if 0 // MILC (does not ! store -w^dgr(x+2)W^dgr(x+1)W^dgr(x). must use c2=-1)
        	U  = PeekIndex<LorentzIndex>(Ulong, mu);
        	UUU = adj( U );
        	UUUdag = Cshift(U, mu, -3);
        #else // USUAL
        	UUU  = PeekIndex<LorentzIndex>(Ulong, mu);
        	UUUdag = adj( Cshift(UUU, mu, -3));
        #endif
        	UUU    = UUU    *KSphases;
          UUU = where(coor == Lmu, bphase * UUU , UUU);

        	UUUdag = UUUdag *KSphases;
          UUUdag = where(coor == 0, conjugate(bphase) * UUUdag , UUUdag);
                
        	InsertGaugeField(UUUds,UUU,mu);
        	InsertGaugeField(UUUds,UUUdag,mu+4);
            
      }
    }

    
    
    
    inline void DoubleStore(GridBase *GaugeGrid,
                            DoubledGaugeField &Uds,
                            const GaugeField &Uthin) {

        typedef typename Simd::scalar_type scalar_type;

        conformable(Uds.Grid(), GaugeGrid);
        conformable(Uthin.Grid(), GaugeGrid);
        GaugeLinkField U(GaugeGrid);
        GaugeLinkField Udag(GaugeGrid);
        Lattice<iScalar<vInteger> > coor(GaugeGrid);
        Lattice<iScalar<vInteger> > x(GaugeGrid); LatticeCoordinate(x,0);
        Lattice<iScalar<vInteger> > y(GaugeGrid); LatticeCoordinate(y,1);
        Lattice<iScalar<vInteger> > z(GaugeGrid); LatticeCoordinate(z,2);
        Lattice<iScalar<vInteger> > t(GaugeGrid); LatticeCoordinate(t,3);
        Lattice<iScalar<vInteger> > lin_z(GaugeGrid); lin_z=x+y;
        Lattice<iScalar<vInteger> > lin_t(GaugeGrid); lin_t=x+y+z;
        ComplexField KSphases(GaugeGrid); 

        for (int mu = 0; mu < Nd; mu++) {
            
      	  int L   = GaugeGrid->GlobalDimensions()[mu];
          int Lmu = L - 1;

          KSphases = 1.0;

          LatticeCoordinate(coor, mu);
              
          ////////// boundary phase /////////////
          auto pha = Params.boundary_phases[mu];
          scalar_type bphase( real(pha),imag(pha) );

          // apply any twists
          RealD theta = Params.twist_n_2pi_L[mu] * 2*M_PI / L;
          if ( theta != 0.0) { 
            scalar_type twphase(::cos(theta),::sin(theta));

            std::cout << GridLogMessage << " Twist [" << mu << "] " << Params.twist_n_2pi_L[mu] << " phase"<< bphase << std::endl;

            bphase = bphase*twphase;
          }

          // Staggered Phase.
      	  if ( mu == 1 ) KSphases = where( mod(x    ,2)==(Integer)0, KSphases,-KSphases);
      	  if ( mu == 2 ) KSphases = where( mod(lin_z,2)==(Integer)0, KSphases,-KSphases);
      	  if ( mu == 3 ) KSphases = where( mod(lin_t,2)==(Integer)0, KSphases,-KSphases);
          
      	  // 1 hop based on fat links
      	  U      = PeekIndex<LorentzIndex>(Uthin, mu);
      	  Udag   = adj( Cshift(U, mu, -1));
                
      	  U = U * KSphases;
          U = where(coor == Lmu, bphase * U , U);

      	  Udag = Udag * KSphases;
          Udag = where(coor == 0, conjugate(bphase) * Udag , Udag);
                
      	  InsertGaugeField(Uds,U,mu);
      	  InsertGaugeField(Uds,Udag,mu+4);
          
        }
    }

  inline void InsertForce4D(GaugeField &mat, FermionField &Btilde, FermionField &A,int mu){
    GaugeLinkField link(mat.Grid());
    link = TraceIndex<SpinIndex>(outerProduct(Btilde,A)); 
    PokeIndex<LorentzIndex>(mat,link,mu);
  }   
      
  inline void InsertForce5D(GaugeField &mat, FermionField &Btilde, FermionField &Atilde,int mu){
    assert (0); 
    // Must never hit
  }
};
typedef StaggeredImpl<vComplex,  FundamentalRepresentation > StaggeredImplR;   // Real.. whichever prec
typedef StaggeredImpl<vComplexF, FundamentalRepresentation > StaggeredImplF;  // Float
typedef StaggeredImpl<vComplexD, FundamentalRepresentation > StaggeredImplD;  // Double

NAMESPACE_END(Grid);
