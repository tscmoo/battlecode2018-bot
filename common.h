
template<typename utype>
struct xy_t {
	utype x {};
	utype y {};
	xy_t() = default;
	xy_t(utype x, utype y) : x(x), y(y) {}
	bool operator<(const xy_t& n) const {
		if (y == n.y) return x < n.x;
		return y < n.y;
	}
	bool operator>(const xy_t& n) const {
		if (y == n.y) return x > n.x;
		return y > n.y;
	}
	bool operator<=(const xy_t& n) const {
		if (y == n.y) return x <= n.x;
		return y <= n.y;
	}
	bool operator>=(const xy_t& n) const {
		if (y == n.y) return x >= n.x;
		return y >= n.y;
	}
	bool operator==(const xy_t& n) const {
		return x == n.x && y == n.y;
	}
	bool operator!=(const xy_t& n) const {
		return x != n.x || y != n.y;
	}
	xy_t operator-(const xy_t& n) const {
		xy_t r(*this);
		return r -= n;
	}
	xy_t& operator-=(const xy_t& n) {
		x -= n.x;
		y -= n.y;
		return *this;
	}
	xy_t operator+(const xy_t& n) const {
		xy_t r(*this);
		return r += n;
	}
	xy_t& operator+=(const xy_t& n) {
		x += n.x;
		y += n.y;
		return *this;
	}
	xy_t operator -() const {
		return xy_t(-x, -y);
	}
	xy_t operator/(const xy_t& n) const {
		xy_t r(*this);
		return r /= n;
	}
	xy_t& operator/=(const xy_t& n) {
		x /= n.x;
		y /= n.y;
		return *this;
	}
	template<typename T>
	xy_t operator/(T&& v) const {
		return xy_t(*this) /= v;
	}
	template<typename T>
	xy_t& operator/=(T&& v) {
		x /= v;
		y /= v;
		return *this;
	}
	xy_t operator*(const xy_t& n) const {
		xy_t r(*this);
		return r *= n;
	}
	xy_t& operator*=(const xy_t& n) {
		x *= n.x;
		y *= n.y;
		return *this;
	}
	template<typename T>
	xy_t operator*(T&& v) const {
		return xy_t(*this) *= v;
	}
	template<typename T>
	xy_t& operator*=(T&& v) {
		x *= v;
		y *= v;
		return *this;
	}
};

using xy = xy_t<int>;


int lengthsq(xy v) {
	return v.x * v.x + v.y * v.y;
}

float length(xy v) {
	return std::sqrt(v.x * v.x + v.y * v.y);
}

template<typename iter_T>
struct iterators_range {
private:
	iter_T begin_it;
	iter_T end_it;
public:
	iterators_range(iter_T begin_it, iter_T end_it) : begin_it(begin_it), end_it(end_it) {}

	using iterator = iter_T;

	using value_type = typename std::iterator_traits<iterator>::value_type;
	using pointer = typename std::iterator_traits<iterator>::pointer;
	using reference = typename std::iterator_traits<iterator>::reference;

	iterator begin() {
		return begin_it;
	}
	iterator end() {
		return end_it;
	}

	bool empty() const {
		return begin_it == end_it;
	}

	reference front() {
		return *begin_it;
	}

};


template<typename iter_T>
iterators_range<iter_T> make_iterators_range(iter_T begin, iter_T end) {
	return iterators_range<iter_T>(begin, end);
}

template<typename cont_T>
auto make_reverse_range(cont_T&& cont) {
	return make_iterators_range(cont.rbegin(), cont.rend());
}

template<typename iterator_T, typename transform_F>
struct transform_iterator {
private:
	typedef transform_iterator this_t;
	iterator_T ptr;
	transform_F f;
public:
	using iterator_category = typename std::iterator_traits<iterator_T>::iterator_category;
	using reference = typename std::result_of<transform_F(typename std::iterator_traits<iterator_T>::reference)>::type;
	using value_type = typename std::remove_cv<typename std::remove_const<reference>::type>::type;
	using difference_type = typename std::iterator_traits<iterator_T>::difference_type;
	using pointer = typename std::remove_reference<reference>::type*;

	template<typename arg_iterator_T, typename arg_transform_F>
	transform_iterator(arg_iterator_T&& ptr, arg_transform_F&& f) : ptr(std::forward<arg_iterator_T>(ptr)), f(std::forward<arg_transform_F>(f)) {}

	reference operator*() const {
		return f(*ptr);
	}
	reference operator*() {
		return f(*ptr);
	}
	this_t& operator++() {
		++ptr;
		return *this;
	}
	this_t operator++(int) {
		auto r = *this;
		++ptr;
		return r;
	}
	this_t& operator--() {
		--ptr;
		return *this;
	}
	this_t operator--(int) {
		auto r = *this;
		--ptr;
		return r;
	}
	this_t& operator+=(difference_type diff) {
		ptr += diff;
		return *this;
	}
	this_t operator+(difference_type diff) const {
		auto r = *this;
		return r += diff;
	}
	this_t& operator-=(difference_type diff) {
		ptr -= diff;
		return *this;
	}
	this_t operator-(difference_type diff) const {
		auto r = *this;
		return r -= diff;
	}
	difference_type operator-(const this_t& other) const {
		return ptr - other.ptr;
	}
	bool operator==(const this_t& rhs) const {
		return ptr == rhs.ptr;
	}
	bool operator!=(const this_t& rhs) const {
		return ptr != rhs.ptr;
	}
	bool operator<(const this_t& rhs) const {
		return ptr < rhs.ptr;
	}
	bool operator<=(const this_t& rhs) const {
		return ptr <= rhs.ptr;
	}
	bool operator>(const this_t& rhs) const {
		return ptr > rhs.ptr;
	}
	bool operator>=(const this_t& rhs) const {
		return ptr >= rhs.ptr;
	}
};

template<typename iterator_T, typename transform_F>
auto make_transform_iterator(iterator_T&& c, transform_F&& f) {
	return transform_iterator<iterator_T, transform_F>(std::forward<iterator_T>(c), std::forward<transform_F>(f));
}

template<typename range_T, typename transform_F>
auto make_transform_range(range_T&& r, transform_F&& f) {
	auto begin = make_transform_iterator(r.begin(), std::forward<transform_F>(f));
	return make_iterators_range(begin, make_transform_iterator(r.end(), std::forward<transform_F>(f)));
}

template<typename iterator_T, typename predicate_F>
struct filter_iterator {
private:
	typedef filter_iterator this_t;
	iterator_T ptr;
	iterator_T end_ptr;
	predicate_F f;
public:
	using iterator_category = std::forward_iterator_tag;
	using reference = typename std::iterator_traits<iterator_T>::reference;
	using value_type = typename std::iterator_traits<iterator_T>::value_type;
	using difference_type = typename std::iterator_traits<iterator_T>::difference_type;
	using pointer = value_type*;

	template<typename arg_iterator_T, typename arg_predicate_F>
	filter_iterator(arg_iterator_T&& ptr, arg_iterator_T&& end_ptr, arg_predicate_F&& f) : ptr(std::forward<arg_iterator_T>(ptr)), end_ptr(std::forward<arg_iterator_T>(end_ptr)), f(std::forward<arg_predicate_F>(f)) {
		if (this->ptr != this->end_ptr && !f(*this->ptr)) ++*this;
	}

	reference operator*() const {
		return *ptr;
	}
	this_t& operator++() {
		do {
			++ptr;
		} while (ptr != end_ptr && !f(*ptr));
		return *this;
	}
	this_t operator++(int) {
		auto r = *this;
		++*this;
		return r;
	}
	bool operator==(const this_t& rhs) const {
		return ptr == rhs.ptr;
	}
	bool operator!=(const this_t& rhs) const {
		return ptr != rhs.ptr;
	}
	bool operator<(const this_t& rhs) const {
		return ptr < rhs.ptr;
	}
	bool operator<=(const this_t& rhs) const {
		return ptr <= rhs.ptr;
	}
	bool operator>(const this_t& rhs) const {
		return ptr > rhs.ptr;
	}
	bool operator>=(const this_t& rhs) const {
		return ptr >= rhs.ptr;
	}
};

template<typename iterator_T, typename predicate_F>
auto make_filter_iterator(iterator_T&& c, iterator_T&& end, predicate_F&& f) {
	return filter_iterator<iterator_T, predicate_F>(std::forward<iterator_T>(c), std::forward<iterator_T>(end), std::forward<predicate_F>(f));
}

template<typename range_T, typename predicate_F>
auto make_filter_range(range_T&& r, predicate_F&& f) {
	auto begin = make_filter_iterator(r.begin(), r.end(), std::forward<predicate_F>(f));
	return make_iterators_range(begin, make_filter_iterator(r.end(), r.end(), std::forward<predicate_F>(f)));
}

template<typename range_T>
auto ptr(range_T&& r) {
	return make_transform_range(r, [](auto& ref) {
		return &ref;
	});
}

template<typename range_T>
auto reverse(range_T&& r) {
	return make_iterators_range(std::make_reverse_iterator(r.end()), std::make_reverse_iterator(r.begin()));
}

template<typename range_T>
auto range_size(range_T&& r) {
	auto rv = std::distance(r.begin(), r.end());
	return (typename std::make_unsigned<decltype(rv)>::type)rv;
}

struct no_value_t {};
static const no_value_t no_value{};
template<typename A,typename B>
bool is_same_but_not_no_value(A&&a,B&&b) {
	return a == b;
}
template<typename A>
bool is_same_but_not_no_value(A&&a,no_value_t) {
	return false;
}
template<typename B>
bool is_same_but_not_no_value(no_value_t,B&&b) {
	return false;
}
struct identity {
	template<typename T>
	decltype(auto) operator()(T&& v) const {
		return std::forward<T>(v);
	}
};

template<typename iterator_T, typename score_F, typename invalid_score_T = no_value_t, typename best_possible_score_T = no_value_t>
auto get_best_score(iterator_T begin, iterator_T end, score_F&& score, invalid_score_T&& invalid_score = invalid_score_T(), best_possible_score_T&& best_possible_score = best_possible_score_T()) {
	if (begin == end) return end;
	auto i = begin;
	auto best = i;
	auto best_score = score(*i);
	++i;
	if (is_same_but_not_no_value(best_score, invalid_score)) {
		best = end;
		for (; i != end; ++i) {
			auto s = score(*i);
			if (is_same_but_not_no_value(s, invalid_score)) continue;
			best = i;
			best_score = s;
			if (is_same_but_not_no_value(s, best_possible_score)) return best;
			break;
		}
	}
	for (; i != end; ++i) {
		auto s = score(*i);
		if (is_same_but_not_no_value(s, invalid_score)) continue;
		if (s < best_score) {
			best = i;
			best_score = s;
			if (is_same_but_not_no_value(s, best_possible_score)) return best;
		}
	}
	return best;
}

template<typename cont_T, typename score_F, typename invalid_score_T = no_value_t, typename best_possible_score_T = no_value_t>
auto get_best_score(cont_T&& cont, score_F&& score, invalid_score_T&& invalid_score = invalid_score_T(), best_possible_score_T&& best_possible_score= best_possible_score_T()) {
	return get_best_score(cont.begin(), cont.end(), std::forward<score_F>(score), std::forward<invalid_score_T>(invalid_score), std::forward<best_possible_score_T>(best_possible_score));
}

template<typename cont_T, typename score_F, typename invalid_score_T = no_value_t, typename best_possible_score_T = no_value_t>
auto get_best_score_copy(cont_T&& cont, score_F&& score, invalid_score_T&& invalid_score = invalid_score_T(), best_possible_score_T&& best_possible_score= best_possible_score_T()) {
	auto i = get_best_score(cont, std::forward<score_F>(score), std::forward<invalid_score_T>(invalid_score), std::forward<best_possible_score_T>(best_possible_score));
	if (i == cont.end()) return typename std::remove_reference<decltype(*i)>::type{};
	return *i;
}

template<typename cont_T, typename score_F, typename invalid_score_T = no_value_t, typename best_possible_score_T = no_value_t>
auto get_best_score_p(cont_T&& cont, score_F&& score, invalid_score_T&& invalid_score = invalid_score_T(), best_possible_score_T&& best_possible_score= best_possible_score_T()) {
	auto i = get_best_score(cont, [&](auto& v) {
		return score(&v);
	}, std::forward<invalid_score_T>(invalid_score), std::forward<best_possible_score_T>(best_possible_score));
	if (i == cont.end()) return (decltype(&*i))nullptr;
	return &*i;
}

template<typename cont_T, typename score_F, typename invalid_score_T = no_value_t, typename best_possible_score_T = no_value_t>
auto get_best_score_value(cont_T&& cont, score_F&& score, invalid_score_T&& invalid_score = invalid_score_T(), best_possible_score_T&& best_possible_score= best_possible_score_T()) {
	auto t_cont = make_transform_range(cont, std::ref(score));
	return get_best_score_copy(t_cont, identity(), std::forward<invalid_score_T>(invalid_score), std::forward<best_possible_score_T>(best_possible_score));
}

template<typename range_T, typename V>
bool range_has(range_T&& r, V&& v) {
	return std::find(r.begin(), r.end(), v) != r.end();
}

template<typename T, typename std::enable_if<std::is_unsigned<T>::value>::type* = nullptr>
T isqrt(T n) {
	T r = 0;
	T p = (T)1 << (8 * sizeof(T) - 2);
	while (p > n) p /= 4u;
	while (p) {
		if (n >= r + p) {
			n -= r + p;
			r += 2u * p;
		}
		r /= 2u;
		p /= 4u;
	}
	return r;
}

template<typename cont_T, typename val_T>
typename cont_T::iterator find_and_erase(cont_T&cont, val_T&&val) {
	return cont.erase(std::find(cont.begin(), cont.end(), std::forward<val_T>(val)));
}

template<typename cont_T, typename val_T>
typename cont_T::iterator find_and_erase_if_exists(cont_T&cont, val_T&&val) {
	auto i = std::find(cont.begin(), cont.end(), std::forward<val_T>(val));
	if (i == cont.end()) return cont.end();
	return cont.erase(i);
}

