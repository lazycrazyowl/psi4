#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <algorithm>
#include <vector>
#include <utility>

#include <psifiles.h>
#include <libciomr/libciomr.h>
#include <libpsio/psio.hpp>
#include <libchkpt/chkpt.hpp>
#include <libiwl/iwl.hpp>
#include <libqt/qt.h>
#include "integralfunctors.h"

#include <libmints/mints.h>
#include "rohf.h"
#include <psi4-dec.h>

#define _DEBUG

using namespace std;
using namespace psi;

namespace psi { namespace scf {

ROHF::ROHF(Options& options, shared_ptr<PSIO> psio, shared_ptr<Chkpt> chkpt)
    : HF(options, psio, chkpt)
{
    common_init();
}

ROHF::ROHF(Options& options, shared_ptr<PSIO> psio)
    : HF(options, psio)
{
    common_init();
}

ROHF::~ROHF() {
}

void ROHF::common_init()
{
    Fa_      = SharedMatrix(factory_->create_matrix("Alpha Fock Matrix"));
    Fb_      = SharedMatrix(factory_->create_matrix("Beta Fock Matrix"));
    Feff_    = SharedMatrix(factory_->create_matrix("F effective (MO basis)"));
    Ca_      = SharedMatrix(factory_->create_matrix("Molecular orbitals"));
    Cb_      = Ca_;
    Da_      = SharedMatrix(factory_->create_matrix("Alpha density matrix"));
    Db_      = SharedMatrix(factory_->create_matrix("Beta density matrix"));
    Ka_      = SharedMatrix(factory_->create_matrix("K alpha"));
    Kb_      = SharedMatrix(factory_->create_matrix("K beta"));
    Ga_      = SharedMatrix(factory_->create_matrix("G alpha"));
    Gb_      = SharedMatrix(factory_->create_matrix("G beta"));
    Dt_old_  = SharedMatrix(factory_->create_matrix("D alpha old"));
    Dt_      = SharedMatrix(factory_->create_matrix("D beta old"));
    moFa_    = SharedMatrix(factory_->create_matrix("MO Basis alpha Fock Matrix"));
    moFb_    = SharedMatrix(factory_->create_matrix("MO Basis beta Fock Matrix"));

    epsilon_a_ = SharedVector(factory_->create_vector());
    epsilon_b_ = epsilon_a_;

    fprintf(outfile, "  DIIS %s.\n\n", diis_enabled_ ? "enabled" : "disabled");
}

void ROHF::finalize()
{
    Feff_.reset();
    Ka_.reset();
    Kb_.reset();
    Ga_.reset();
    Gb_.reset();
    Dt_old_.reset();
    Dt_.reset();
    moFa_.reset();
    moFb_.reset();

    HF::finalize();
}

void ROHF::form_initial_C()
{
    SharedMatrix temp = shared_ptr<Matrix>(factory_->create_matrix());

    // In ROHF the creation of the C matrix depends on the previous iteration's C
    // matrix. Here we use H to generate the first C.
    temp->copy(H_);
    temp->transform(Shalf_);
    temp->diagonalize(Ca_, epsilon_a_);
    find_occupation();
    temp->gemm(false, false, 1.0, Shalf_, Ca_, 0.0);
    Ca_->copy(temp);

    if (print_ > 3)
        Ca_->print(outfile, "initial C");
}

void ROHF::save_density_and_energy()
{
    Dt_old_->copy(Dt_);
    Eold_ = E_; // save previous energy
}

void ROHF::save_information()
{
    // Print the final docc vector
    char **temp2 = molecule_->irrep_labels();

    fprintf(outfile, "\n  Final DOCC vector = (");
    for (int h=0; h<factory_->nirrep(); ++h) {
        fprintf(outfile, "%2d %3s ", doccpi_[h], temp2[h]);
    }
    fprintf(outfile, ")\n");

    fprintf(outfile, "  Final SOCC vector = (");
    for (int h=0; h<factory_->nirrep(); ++h) {
        fprintf(outfile, "%2d %3s ", soccpi_[h], temp2[h]);
    }
    fprintf(outfile, ")\n");

    int print_mos = options_.get_bool("PRINT_MOS");
    if (print_mos) {
        fprintf(outfile, "\n  Molecular orbitals:\n");
        Ca_->eivprint(epsilon_a_);
    }

    // Print out orbital energies.
    std::vector<std::pair<double, int> > pairs;
    for (int h=0; h<epsilon_a_->nirrep(); ++h) {
        for (int i=0; i<epsilon_a_->dimpi()[h]; ++i)
            pairs.push_back(make_pair(epsilon_a_->get(h, i), h));
    }
    sort(pairs.begin(), pairs.end());
    int ndocc = 0, nsocc = 0;
    for (int i=0; i<epsilon_a_->nirrep(); ++i) {
        ndocc += doccpi_[i];
        nsocc += soccpi_[i];
    }

    fprintf(outfile,
            "\n  Orbital energies (a.u.):\n    Doubly occupied orbitals\n      ");
    for (int i=1; i<=ndocc; ++i) {
        fprintf(outfile, "%12.6f %3s  ", pairs[i-1].first,
                temp2[pairs[i-1].second]);
        if (i % 4 == 0)
            fprintf(outfile, "\n      ");
    }
    fprintf(outfile, "\n");
    fprintf(outfile, "\n    Singly occupied orbitals\n      ");
    for (int i=ndocc+1; i<=ndocc+nsocc; ++i) {
        fprintf(outfile, "%12.6f %3s  ", pairs[i-1].first,
                temp2[pairs[i-1].second]);
        if ((i-ndocc) % 4 == 0)
            fprintf(outfile, "\n      ");
    }
    fprintf(outfile, "\n");
    fprintf(outfile, "\n    Unoccupied orbitals\n      ");
    for (int i=ndocc+nsocc+1; i<=nso_; ++i) {
        fprintf(outfile, "%12.6f %3s  ", pairs[i-1].first,
                temp2[pairs[i-1].second]);
        if ((i-ndocc-nsocc) % 4 == 0)
            fprintf(outfile, "\n      ");
    }
    fprintf(outfile, "\n");

    for (int i=0; i<epsilon_a_->nirrep(); ++i)
        free(temp2[i]);
    free(temp2);
}

void ROHF::save_fock()
{
    if (initialized_diis_manager_ == false) {
        diis_manager_ = shared_ptr<DIISManager>(new DIISManager(max_diis_vectors_, "HF DIIS vector", DIISManager::LargestError, DIISManager::OnDisk, psio_));
        diis_manager_->set_error_vector_size(1, DIISEntry::Matrix, Feff_.get());
        diis_manager_->set_vector_size(1, DIISEntry::Matrix, Feff_.get());
        initialized_diis_manager_ = true;
    }

    // Save the effective Fock, back transform to AO, and orthonormalize
//    diis_F_[current_diis_fock_]->copy(Feff_);
//    diis_F_[current_diis_fock_]->back_transform(C_);
//    diis_F_[current_diis_fock_]->transform(Sphalf_);

//    // Determine error matrix for this Fock
//    diis_E_[current_diis_fock_]->copy(Feff_);
//    diis_E_[current_diis_fock_]->zero_diagonal();
//    diis_E_[current_diis_fock_]->back_transform(C_);
//    diis_E_[current_diis_fock_]->transform(Sphalf_);

//#ifdef _DEBUG
//    if (debug_) {
//        fprintf(outfile, "  New error matrix:\n");
//        diis_E_[current_diis_fock_]->print(outfile);
//    }
//#endif
//    current_diis_fock_++;
//    if (current_diis_fock_ == min_diis_vectors_)
//        current_diis_fock_ = 0;
}

bool ROHF::diis()
{
    return diis_manager_->extrapolate(1, Feff_.get());
}

bool ROHF::test_convergency()
{
    // energy difference
    double ediff = E_ - Eold_;

    // RMS of the density
    Matrix D_rms;
    D_rms.copy(Dt_);
    D_rms.subtract(Dt_old_);
    Drms_ = D_rms.rms();

    if (fabs(ediff) < energy_threshold_ && Drms_ < density_threshold_)
        return true;
    else
        return false;
}

void ROHF::form_initialF()
{
    // Form the initial Fock matrix, closed and open variants
    Fa_->copy(H_);
    Fa_->transform(Shalf_);
    Fb_->copy(Fa_);

#ifdef _DEBUG
    if (debug_) {
        fprintf(outfile, "Initial alpha Fock matrix:\n");
        Fa_->print(outfile);
        fprintf(outfile, "Initial beta Fock matrix:\n");
        Fb_->print(outfile);
    }
#endif
}

void ROHF::form_F()
{
    // Start by constructing the standard Fa and Fb matrices encountered in UHF
    Fa_->copy(H_);
    Fb_->copy(H_);
    Fa_->add(Ga_);
    Fb_->add(Gb_);

    moFa_->transform(Fa_, Ca_);
    moFb_->transform(Fb_, Ca_);

    /*
     * Fo = open-shell fock matrix = 0.5 Fa
     * Fc = closed-shell fock matrix = 0.5 (Fa + Fb)
     *
     * Therefore
     *
     * 2(Fc-Fo) = Fb
     * 2Fo = Fa
     *
     * Form the effective Fock matrix, too
     * The effective Fock matrix has the following structure
     *          |  closed     open    virtual
     *  ----------------------------------------
     *  closed  |    Fc     2(Fc-Fo)    Fc
     *  open    | 2(Fc-Fo)     Fc      2Fo
     *  virtual |    Fc       2Fo       Fc
     */
    Feff_->copy(moFa_);
    Feff_->add(moFb_);
    Feff_->scale(0.5);
    for (int h = 0; h < nirrep_; ++h) {
        for (int i = doccpi_[h]; i < doccpi_[h] + soccpi_[h]; ++i) {
            // Set the open/closed portion
            for (int j = 0; j < doccpi_[h]; ++j) {
                double val = moFb_->get(h, i, j);
                Feff_->set(h, i, j, val);
                Feff_->set(h, j, i, val);
            }
            // Set the open/virtual portion
            for (int j = doccpi_[h] + soccpi_[h]; j < nmopi_[h]; ++j) {
                double val = moFa_->get(h, i, j);
                Feff_->set(h, i, j, val);
                Feff_->set(h, j, i, val);
            }
        }
    }

    if (debug_) {
        Fa_->print();
        Fb_->print();
        moFa_->print();
        moFb_->print();
        Feff_->print(outfile);
    }
}

void ROHF::form_C()
{
    SharedMatrix temp(factory_->create_matrix());
    SharedMatrix eigvec(factory_->create_matrix());

    // Obtain new eigenvectors
    Feff_->diagonalize(eigvec, epsilon_a_);
    find_occupation();

    if (debug_) {
        fprintf(outfile, "In ROHF::form_C:\n");
        eigvec->eivprint(epsilon_a_);
    }
    temp->gemm(false, false, 1.0, Ca_, eigvec, 0.0);
    Ca_->copy(temp);

    if (debug_) {
        Ca_->print(outfile);
    }
}

void ROHF::form_D()
{
    for (int h = 0; h < nirrep_; ++h) {
        for (int i = 0; i < nsopi_[h]; ++i) {
            for (int j = 0; j < nsopi_[h]; ++j) {
                double val = 0.0;
                for(int m = 0; m < doccpi_[h]; ++m)
                    val += Ca_->get(h, i, m) * Ca_->get(h, j, m);
                Db_->set(h, i, j, val);

                for(int m = doccpi_[h]; m < doccpi_[h] + soccpi_[h]; ++m)
                    val += Ca_->get(h, i, m) * Ca_->get(h, j, m);
                Da_->set(h, i, j, val);
            }
        }
    }

    Dt_->copy(Da_);
    Dt_->copy(Db_);

    if (debug_) {
        fprintf(outfile, "in ROHF::form_D:\n");
        Da_->print();
        Db_->print();
    }
}

double ROHF::compute_initial_E()
{
    return compute_E();
}

double ROHF::compute_E() {
    double DH  = Da_->vector_dot(H_);
    DH += Db_->vector_dot(H_);
    double DFa = Da_->vector_dot(Fa_);
    double DFb = Db_->vector_dot(Fb_);
    double Eelec = 0.5 * (DH + DFa + DFb);
    double Etotal = nuclearrep_ + Eelec;
    return Etotal;
}

void ROHF::form_G()
{
    /*
     * This just builds the same Ga and Gb matrices used in UHF
     */
    J_Ka_Kb_Functor jk_builder(Ga_, Ka_, Kb_, Da_, Db_, Ca_, Cb_, nalphapi_, nbetapi_);
    process_tei<J_Ka_Kb_Functor>(jk_builder);

    Gb_->copy(Ga_);
    Ga_->subtract(Ka_);
    Gb_->subtract(Kb_);
}

}}
