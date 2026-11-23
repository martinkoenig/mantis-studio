extern "C" int fixture_value();

int main() {
    return fixture_value() == 42 ? 0 : 1;
}
