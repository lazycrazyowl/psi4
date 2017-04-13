/* Implements ecp.hpp 

Robert A. Shaw 2016
*/

#include "psi4/libmints/ecp.h"
#include "coordentry.h"


#include <cmath>
#include <iostream>
#include <stdexcept>
#include <map>


namespace psi {

	std::vector <ECPShellInfo>
	Gaussian94ECPBasisSetParser::ecp_parse(const std::string &symbol, const std::vector <std::string> &lines)
	{
		std::vector <ECPShellInfo> shell_list;
		
		// TODO: Work out how to parse
		
	    // The constructor, or the caller, should refresh the basis set.
	    return shell_list;
	}

	// class ECPShellInfo
	ECPShellInfo::ECPShellInfo(int am, const std::vector<double>& c, const std::vector<double>& e,
	const std::vector<int>& n, const std::vector<int>& sub_l, int nc, const Vector3& center,
	int start) : ShellInfo(am, c, e, GaussianType::Cartesian, nc, center, start), n_(n), sub_l_(sub_l) 
	{}

	ECPShellInfo ECPShellInfo::copy()
	{
		return ECPShellInfo(l_, original_coef_, exp_, n_, sub_l_,
			nc_, center_, start_);
	}

	ECPShellInfo ECPShellInfo::copy(int nc, const Vector3& c)
	{
		return ECPShellInfo(l_, original_coef_, exp_, n_, sub_l_,
		nc, c, start_);
	}	
	
	bool ECPShellInfo::operator==(const ECPShellInfo& RHS) const{
	    return(l_==RHS.l_ &&
	           puream_==RHS.puream_ &&
	           exp_==RHS.exp_ &&
	           original_coef_==RHS.erd_coef_ &&
			   n_==RHS.n_ &&
			   sub_l_ == RHS.sub_l_ &&
	           nc_==RHS.nc_ &&
	           center_==RHS.center_ &&
	           start_==RHS.start_ &&
	           ncartesian_==RHS.ncartesian_ &&
	           nfunction_==RHS.nfunction_
	    );
	}
	
	GaussianECPShell::GaussianECPShell(int am, int nprimitive, const double *oc, const double *e, const int *n,
					 const int *subl, int nc, const double* center, int start) 
						 : GaussianShell(am, nprimitive, oc, oc, oc, e, GaussianType::Cartesian, nc, center, start),
                     n_(n), sub_l_(subl) {}

	// class GaussianECPShell
	// Evaluate U_l(r), assuming that gaussians sorted by angular momentum
	double GaussianECPShell::evaluate(double r, int l) const {
		double value = 0.0;
		double r2 = r*r;
		for (int i = 0; i < nprimitive_; i++) {
			if (sub_l_[i] == l) 
				value += pow(r, n_[i]) * original_coef_[i] * exp(-exp_[i] * r2);
		} 
		return value; 
	}
	
	ECPBasisSet::ECPBasisSet() : BasisSet() {} 
	ECPBasisSet::ECPBasisSet(const std::string &basistype, SharedMolecule mol,
                std::map<std::string, std::map<std::string, std::vector<ECPShellInfo> > > &shell_map) 
{
	name_ = basistype;
	molecule_ = mol; 
	
    // Singletons
    initialize_singletons();

    int natom = molecule_->natom();

    /// These will tell us where the primitives for [basis][symbol] start and end, in the compact array
    std::map <std::string, std::map<std::string, int>> primitive_start;
    std::map <std::string, std::map<std::string, int>> primitive_end;

    /*
     * First, loop over the unique primitives, and store them
     */
    std::vector<double> uexps;
    std::vector<double> ucoefs;
	std::vector<int> uns;
	std::vector<int> usubls; 
    std::vector<double> uoriginal_coefs;
    std::vector<double> uerd_coefs;
    n_uprimitive_ = 0;
    std::map < std::string, std::map < std::string, std::vector < ECPShellInfo > > > ::iterator basis_iter;
    for (basis_iter = shell_map.begin(); basis_iter != shell_map.end(); ++basis_iter) {
        const std::string &basis = basis_iter->first;
        std::map <std::string, std::vector<ECPShellInfo>> &symbol_map = shell_map[basis];
        std::map < std::string, std::vector < ECPShellInfo > > ::iterator symbol_iter;
        for (symbol_iter = symbol_map.begin(); symbol_iter != symbol_map.end(); ++symbol_iter) {
            const std::string &label = symbol_iter->first;  // symbol --> label
            std::vector <ECPShellInfo> &shells = symbol_map[label];  // symbol --> label
            primitive_start[basis][label] = n_uprimitive_;  // symbol --> label
            for (size_t i = 0; i < shells.size(); ++i) {
                const ECPShellInfo &shell = shells[i];
                for (int prim = 0; prim < shell.nprimitive(); ++prim) {
                    uexps.push_back(shell.exp(prim));
                    ucoefs.push_back(shell.coef(prim));
                    uoriginal_coefs.push_back(shell.original_coef(prim));
                    uerd_coefs.push_back(0); // These are not needed for the ECP basis, but are left in for now
					uns.push_back(shell.n(prim));
					usubls.push_back(shell.subl(prim));
                    n_uprimitive_++;
                }
            }
            primitive_end[basis][label] = n_uprimitive_;  // symbol --> label
        }
    }

    /*
     * Count basis functions, shells and primitives
     */
    n_shells_ = 0;
    nprimitive_ = 0;
    nao_ = 0;
    nbf_ = 0;
    for (int n = 0; n < natom; ++n) {
        const std::shared_ptr <CoordEntry> &atom = molecule_->atom_entry(n);
        std::string basis = atom->basisset(basistype);
        std::string label = atom->label();  // symbol --> label
        std::vector <ECPShellInfo> &shells = shell_map[basis][label];  // symbol --> label
        for (size_t i = 0; i < shells.size(); ++i) {
            const ECPShellInfo &shell = shells[i];
            int nprim = shell.nprimitive();
            nprimitive_ += nprim;
            n_shells_++;
            nao_ += shell.ncartesian();
            nbf_ += shell.nfunction();
        }
    }

    /*
     * Allocate arrays
     */
    n_prim_per_shell_ = new int[n_shells_];
    // The unique primitives
    uexponents_ = new double[n_uprimitive_];
    ucoefficients_ = new double[n_uprimitive_];
    uoriginal_coefficients_ = new double[n_uprimitive_];
    uerd_coefficients_ = new double[n_uprimitive_];
	uns_ = new int[n_uprimitive_];
	usubls_ = new int[n_uprimitive_];
    for (int i = 0; i < n_uprimitive_; ++i) {
        uexponents_[i] = uexps[i];
        ucoefficients_[i] = ucoefs[i];
        uoriginal_coefficients_[i] = uoriginal_coefs[i];
        uerd_coefficients_[i] = uerd_coefs[i];
		uns_[i] = uns[i];
		usubls_[i] = usubls[i];
    }

    shell_first_ao_ = new int[n_shells_];
    shell_first_basis_function_ = new int[n_shells_];
    shells_ = new GaussianShell[n_shells_];
    ao_to_shell_ = new int[nao_];
    function_to_shell_ = new int[nbf_];
    function_center_ = new int[nbf_];
    shell_center_ = new int[n_shells_];
    center_to_nshell_ = new int[natom];
    center_to_shell_ = new int[natom];
    xyz_ = new double[3 * natom];

    /*
     * Now loop over all atoms, and point to the appropriate unique data
     */
    int shell_count = 0;
    int ao_count = 0;
    int bf_count = 0;
    double *xyz_ptr = xyz_;
    puream_ = false;
    max_am_ = 0;
    max_nprimitive_ = 0;
    for (int n = 0; n < natom; ++n) {
        const std::shared_ptr <CoordEntry> &atom = molecule_->atom_entry(n);
        std::string basis = atom->basisset(basistype);
        std::string label = atom->label();  // symbol --> label
        std::vector <ECPShellInfo> &shells = shell_map[basis][label];  // symbol --> label
        int ustart = primitive_start[basis][label];  // symbol --> label
        int uend = primitive_end[basis][label];  // symbol --> label
        int nshells = shells.size();
        center_to_nshell_[n] = nshells;
        center_to_shell_[n] = shell_count;
        int atom_nprim = 0;
        for (int i = 0; i < nshells; ++i) {
            const ECPShellInfo &thisshell = shells[i];
            shell_first_ao_[shell_count] = ao_count;
            shell_first_basis_function_[shell_count] = bf_count;
            int shell_nprim = thisshell.nprimitive();
            int am = thisshell.am();
            max_nprimitive_ = shell_nprim > max_nprimitive_ ? shell_nprim : max_nprimitive_;
            max_am_ = max_am_ > am ? max_am_ : am;
            shell_center_[shell_count] = n;
            GaussianType puream = thisshell.is_pure() ? Pure : Cartesian;
            if (puream)
                puream_ = true;
            //            outfile->Printf( "atom %d basis %s shell %d nprim %d atom_nprim %d\n", n, basis.c_str(), i, shell_nprim, atom_nprim);
            shells_[shell_count] = GaussianECPShell(am, shell_nprim, &uoriginal_coefficients_[ustart + atom_nprim],
                                                  &uexponents_[ustart + atom_nprim], &uns_[ustart+atom_nprim], &usubls_[ustart+atom_nprim],
												  n, xyz_ptr, bf_count);
            for (int thisbf = 0; thisbf < thisshell.nfunction(); ++thisbf) {
                function_to_shell_[bf_count] = shell_count;
                function_center_[bf_count++] = n;
            }
            for (int thisao = 0; thisao < thisshell.ncartesian(); ++thisao) {
                ao_to_shell_[ao_count++] = shell_count;
            }
            atom_nprim += shell_nprim;
            shell_count++;
        }
        Vector3 xyz = molecule_->xyz(n);
        xyz_ptr[0] = xyz[0];
        xyz_ptr[1] = xyz[1];
        xyz_ptr[2] = xyz[2];
        xyz_ptr += 3;
        if (atom_nprim != uend - ustart) {
            throw PSIEXCEPTION("Problem with nprimitive in basis set construction!");
        }
    }
}

    std::shared_ptr<ECPBasisSet> ECPBasisSet::construct_ecp_from_pydict(std::shared_ptr <Molecule> mol, py::dict pybs, const int forced_puream){

        std::string key = pybs["key"].cast<std::string>();
        std::string name = pybs["name"].cast<std::string>();
        std::string label = pybs["blend"].cast<std::string>();
        std::string message = pybs["message"].cast<std::string>();

        mol->set_basis_all_atoms(name, key);

        // Map of GaussianShells: basis_atom_shell[basisname][atomlabel] = gaussian_shells
        typedef std::map <std::string, std::map<std::string, std::vector < ECPShellInfo>>> map_ssv;
        map_ssv basis_atom_shell;
        std::map< std::string, std::map<std::string, int>> basis_atom_ncore;
        // basisname is uniform; fill map with key/value (gbs entry) pairs of elements from pybs['shell_map']
        py::list basisinfo = pybs["ecp_shell_map"].cast<py::list>();
        if(len(basisinfo) == 0)
            throw PSIEXCEPTION("Empty ECP information being used to construct ECPBasisSet.");
        for(int atom = 0; atom < py::len(basisinfo); ++atom){
            std::vector<ECPShellInfo> vec_shellinfo;
            py::list atominfo = basisinfo[atom].cast<py::list>();
            std::string atomlabel = atominfo[0].cast<std::string>();
            std::string hash = atominfo[1].cast<std::string>();
            int ncore = atominfo[2].cast<int>();
            for(int atomshells = 3; atomshells < py::len(atominfo); ++atomshells){
                // Each shell entry has p primitives that look like
                // [ angmom, [ [ e1, c1, r1 ], [ e2, c2, r2 ], ...., [ ep, cp, rp ] ] ]
                py::list shellinfo = atominfo[atomshells].cast<py::list>();
                int am = shellinfo[0].cast<int>();
                std::vector<double> coefficients;
                std::vector<double> exponents;
                std::vector<int> ns;
                std::vector<int> sub_ls;
                int nprim = (pybind11::len(shellinfo)) - 1; // The leading entry is the angular momentum
                for (int primitive = 1; primitive <= nprim; primitive++) {
                    py::list primitiveinfo = shellinfo[primitive].cast<py::list>();
                    exponents.push_back(primitiveinfo[0].cast<double>());
                    coefficients.push_back(primitiveinfo[1].cast<double>());
                    ns.push_back(primitiveinfo[2].cast<int>());
                    sub_ls.push_back(am);
                }
                Vector3 fake_center; // TODO the center information is not used; we should rip it out of here and GShell
                vec_shellinfo.push_back(ECPShellInfo(am, coefficients, exponents, ns, sub_ls, 0, fake_center, 0));
            }
            mol->set_shell_by_label(atomlabel, hash, key);
            basis_atom_ncore[name][atomlabel] = ncore;
            basis_atom_shell[name][atomlabel] = vec_shellinfo;
        }
        mol->update_geometry();  // update symmetry with basisset info

        // Modify the nuclear charges, to account for the ECP.  Currently this assumes that the regular basis set
        // has a pointer to the same molecule object, so these changes will propagate properly.  We may need to
        // rethink that strategy at some point in the future.
        for(int atom=0; atom<mol->natom(); ++atom){
            const std::string &basis = mol->basis_on_atom(atom);
            const std::string &label = mol->label(atom);
            int ncore = basis_atom_ncore[basis][label];
            int Z = mol->Z(atom);
            Z -= ncore;
            mol->set_nuclear_charge(atom, Z);
        }
        std::shared_ptr <ECPBasisSet> basisset(new ECPBasisSet(key, mol, basis_atom_shell));

        basisset->name_.clear();
        basisset->name_ = name;
        basisset->key_ = key;
        basisset->target_ = label;
        return basisset;
    }

}


